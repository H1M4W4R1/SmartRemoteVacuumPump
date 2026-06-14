#include <operation/fw_controller_modes.h>

#include <assert.h>

#include <operation/fw_config.h>
#include <operation/fw_controller_common.h>

static uint32_t g_adaptiveLastMs = 0UL;
static int32_t g_adaptiveLastErrorHpa = 0L;
static int32_t g_adaptiveIntegralHpaMs = 0L;
static int32_t g_adaptiveResponseX1000 = FW_ADAPTIVE_RESPONSE_START_X1000;
static int32_t g_adaptiveLeakX1000 = 0L;
static FwMode g_adaptiveLastMode = FW_MODE_MANUAL;
static int32_t g_adaptiveLastTargetHpa = 0L;

static int32_t fwControllerAdaptiveError(const FwConfig *config, int32_t pressureHpa)
{
    int32_t sign = 1L;
    assert(config != nullptr);
    assert(pressureHpa < 2000L);
    if (config == nullptr) {
        return 0L;
    }
    if (config->targetPressureHpa < 0L) {
        sign = -1L;
    }
    return sign * (config->targetPressureHpa - pressureHpa);
}

static bool fwControllerAdaptiveReset(const FwConfig *config, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(pressureHpa > -2000L);
    if (config == nullptr) {
        return false;
    }
    g_adaptiveLastMs = millis();
    g_adaptiveLastErrorHpa = fwControllerAdaptiveError(config, pressureHpa);
    g_adaptiveIntegralHpaMs = 0L;
    g_adaptiveResponseX1000 = FW_ADAPTIVE_RESPONSE_START_X1000;
    g_adaptiveLeakX1000 = 0L;
    g_adaptiveLastMode = config->mode;
    g_adaptiveLastTargetHpa = config->targetPressureHpa;
    return true;
}

static bool fwControllerAdaptiveRefresh(const FwConfig *config, int32_t pressureHpa)
{
    bool changed = false;
    assert(config != nullptr);
    assert(config->mode <= FW_MODE_TARGET_ADAPTIVE);
    if (config == nullptr) {
        return false;
    }
    changed = (g_adaptiveLastMode != config->mode)
        || (g_adaptiveLastTargetHpa != config->targetPressureHpa)
        || (g_adaptiveLastMs == 0UL);
    if (changed) {
        return fwControllerAdaptiveReset(config, pressureHpa);
    }
    return true;
}

static bool fwControllerAdaptiveCalibrate(const FwConfig *config, const FwSession *session,
    int32_t errorHpa, uint32_t elapsedMs)
{
    int32_t errorRateX1000 = 0L;
    int32_t responseX1000 = 0L;
    int32_t leakX1000 = 0L;
    int32_t pumpDutyX1000 = 0L;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr) || (elapsedMs == 0UL)) {
        return false;
    }
    errorRateX1000 = ((g_adaptiveLastErrorHpa - errorHpa) * 1000L) / (int32_t)elapsedMs;
    errorRateX1000 *= 1000L;
    pumpDutyX1000 = fwControllerCommonPumpDutyX1000();
    if (session->pumpActive && (pumpDutyX1000 > 0L) && (errorRateX1000 > 0L)) {
        responseX1000 = (errorRateX1000 * 1000L) / pumpDutyX1000;
        responseX1000 = ((g_adaptiveResponseX1000 * 7L) + responseX1000) / 8L;
        g_adaptiveResponseX1000 = fwControllerCommonClampInt32(responseX1000,
            FW_ADAPTIVE_RESPONSE_MIN_X1000, 200000L);
    }
    if (!session->pumpActive && (errorHpa > 0L) && (errorRateX1000 < 0L)) {
        leakX1000 = ((g_adaptiveLeakX1000 * 7L) - errorRateX1000) / 8L;
        g_adaptiveLeakX1000 = fwControllerCommonClampInt32(leakX1000, 0L, 200000L);
    }
    g_adaptiveLastErrorHpa = errorHpa;
    return config->mode == FW_MODE_TARGET_ADAPTIVE;
}

static int32_t fwControllerAdaptiveFeedForward(void)
{
    int32_t dutyX1000 = 0L;
    assert(g_adaptiveResponseX1000 >= FW_ADAPTIVE_RESPONSE_MIN_X1000);
    assert(FW_ADAPTIVE_RESPONSE_MIN_X1000 > 0L);
    if (g_adaptiveResponseX1000 < FW_ADAPTIVE_RESPONSE_MIN_X1000) {
        return 0L;
    }
    dutyX1000 = (g_adaptiveLeakX1000 * 1000L) / g_adaptiveResponseX1000;
    return fwControllerCommonClampInt32(dutyX1000, 0L, 900L);
}

static int32_t fwControllerAdaptivePid(int32_t errorHpa, uint32_t elapsedMs)
{
    int32_t derivativeHpaSec = 0L;
    int32_t dutyX1000 = 0L;
    assert(elapsedMs > 0UL);
    assert(FW_ADAPTIVE_PID_KP_X1000 > 0L);
    if (elapsedMs == 0UL) {
        return 0L;
    }
    if (errorHpa <= 0L) {
        g_adaptiveIntegralHpaMs = 0L;
        return 0L;
    }
    g_adaptiveIntegralHpaMs += errorHpa * (int32_t)elapsedMs;
    g_adaptiveIntegralHpaMs = fwControllerCommonClampInt32(g_adaptiveIntegralHpaMs,
        -FW_ADAPTIVE_PID_MAX_INTEGRAL_HPA_MS, FW_ADAPTIVE_PID_MAX_INTEGRAL_HPA_MS);
    derivativeHpaSec = ((errorHpa - g_adaptiveLastErrorHpa) * 1000L) / (int32_t)elapsedMs;
    dutyX1000 = (errorHpa * FW_ADAPTIVE_PID_KP_X1000) / 1000L;
    dutyX1000 += (g_adaptiveIntegralHpaMs / 1000L) * FW_ADAPTIVE_PID_KI_X1000 / 1000L;
    dutyX1000 += (derivativeHpaSec * FW_ADAPTIVE_PID_KD_X1000) / 1000L;
    dutyX1000 += fwControllerAdaptiveFeedForward();
    return fwControllerCommonClampInt32(dutyX1000, 0L, 1000L);
}

static bool fwControllerAdaptiveApply(FwSession *session, int32_t dutyX1000)
{
    bool ok = false;
    assert(session != nullptr);
    assert(dutyX1000 <= 1000L);
    if (session == nullptr) {
        return false;
    }
    ok = fwControllerCommonSetPumpDutyX1000(dutyX1000);
    if (!ok) {
        return false;
    }
    session->pumpActive = dutyX1000 > 0L;
    return true;
}

bool fwControllerTargetAdaptivePoll(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    uint32_t nowMs = millis();
    uint32_t elapsedMs = 0UL;
    int32_t errorHpa = 0L;
    int32_t dutyX1000 = 0L;
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = fwControllerAdaptiveRefresh(config, pressureHpa);
    if (!ok) {
        return false;
    }
    elapsedMs = nowMs - g_adaptiveLastMs;
    elapsedMs = (elapsedMs == 0UL) ? 1UL : elapsedMs;
    elapsedMs = (elapsedMs > 1000UL) ? 1000UL : elapsedMs;
    errorHpa = fwControllerAdaptiveError(config, pressureHpa);
    if (session->temporaryPumpDisabled) {
        g_adaptiveIntegralHpaMs = 0L;
        ok = fwControllerAdaptiveCalibrate(config, session, errorHpa, elapsedMs);
        ok = fwControllerAdaptiveApply(session, 0L) && ok;
        g_adaptiveLastMs = nowMs;
        return fwControllerCommonApplyOutputs(session) && ok;
    }
    dutyX1000 = fwControllerAdaptivePid(errorHpa, elapsedMs);
    ok = fwControllerAdaptiveCalibrate(config, session, errorHpa, elapsedMs);
    ok = fwControllerAdaptiveApply(session, dutyX1000) && ok;
    g_adaptiveLastMs = nowMs;
    return fwControllerCommonApplyOutputs(session) && ok;
}
