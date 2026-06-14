#include <operation/fw_controller.h>

#include <assert.h>

#include <operation/fw_config.h>
#include <systems/sys_outputs.h>
#include <systems/sys_pressure.h>

static uint32_t g_lastTickMs = 0UL;
static uint32_t g_secondAccumulatorMs = 0UL;
static uint32_t g_adaptiveLastMs = 0UL;
static int32_t g_adaptiveLastErrorHpa = 0L;
static int32_t g_adaptiveIntegralHpaMs = 0L;
static int32_t g_adaptiveResponseX1000 = FW_ADAPTIVE_RESPONSE_START_X1000;
static int32_t g_adaptiveLeakX1000 = 0L;
static int32_t g_pumpDutyX1000 = 1000L;
static FwMode g_adaptiveLastMode = FW_MODE_MANUAL;
static int32_t g_adaptiveLastTargetHpa = 0L;

static bool fwControllerApplyOutputs(const FwSession *session);

static int32_t fwControllerClampInt32(int32_t value, int32_t minimum, int32_t maximum)
{
    assert(minimum <= maximum);
    assert(maximum > -2147483000L);
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int32_t fwControllerTargetSign(const FwConfig *config)
{
    assert(config != nullptr);
    assert(FW_ADAPTIVE_PID_KP_X1000 > 0L);
    if (config == nullptr) {
        return 1L;
    }
    if (config->targetPressureHpa < 0L) {
        return -1L;
    }
    return 1L;
}

static int32_t fwControllerAdaptiveError(const FwConfig *config, int32_t pressureHpa)
{
    int32_t sign = 1L;
    assert(config != nullptr);
    assert(pressureHpa < 2000L);
    if (config == nullptr) {
        return 0L;
    }
    sign = fwControllerTargetSign(config);
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
    int32_t pressureHpa, int32_t errorHpa, uint32_t elapsedMs)
{
    int32_t errorRateX1000 = 0L;
    int32_t responseX1000 = 0L;
    int32_t leakX1000 = 0L;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr) || (elapsedMs == 0UL)) {
        return false;
    }
    errorRateX1000 = ((g_adaptiveLastErrorHpa - errorHpa) * 1000L) / (int32_t)elapsedMs;
    errorRateX1000 *= 1000L;
    if (session->pumpActive && (g_pumpDutyX1000 > 0L) && (errorRateX1000 > 0L)) {
        responseX1000 = (errorRateX1000 * 1000L) / g_pumpDutyX1000;
        responseX1000 = ((g_adaptiveResponseX1000 * 7L) + responseX1000) / 8L;
        g_adaptiveResponseX1000 = fwControllerClampInt32(responseX1000,
            FW_ADAPTIVE_RESPONSE_MIN_X1000, 200000L);
    }
    if (!session->pumpActive && (errorHpa > 0L) && (errorRateX1000 < 0L)) {
        leakX1000 = ((g_adaptiveLeakX1000 * 7L) - errorRateX1000) / 8L;
        g_adaptiveLeakX1000 = fwControllerClampInt32(leakX1000, 0L, 200000L);
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
    return fwControllerClampInt32(dutyX1000, 0L, 900L);
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
    g_adaptiveIntegralHpaMs = fwControllerClampInt32(g_adaptiveIntegralHpaMs,
        -FW_ADAPTIVE_PID_MAX_INTEGRAL_HPA_MS, FW_ADAPTIVE_PID_MAX_INTEGRAL_HPA_MS);
    derivativeHpaSec = ((errorHpa - g_adaptiveLastErrorHpa) * 1000L) / (int32_t)elapsedMs;
    dutyX1000 = (errorHpa * FW_ADAPTIVE_PID_KP_X1000) / 1000L;
    dutyX1000 += (g_adaptiveIntegralHpaMs / 1000L) * FW_ADAPTIVE_PID_KI_X1000 / 1000L;
    dutyX1000 += (derivativeHpaSec * FW_ADAPTIVE_PID_KD_X1000) / 1000L;
    dutyX1000 += fwControllerAdaptiveFeedForward();
    return fwControllerClampInt32(dutyX1000, 0L, 1000L);
}

static bool fwControllerAdaptiveApply(FwSession *session, int32_t dutyX1000, uint32_t nowMs)
{
    assert(session != nullptr);
    assert(dutyX1000 <= 1000L);
    if (session == nullptr) {
        return false;
    }
    (void)nowMs;
    g_pumpDutyX1000 = dutyX1000;
    session->pumpActive = dutyX1000 > 0L;
    return true;
}

static bool fwControllerAdaptiveKeep(FwConfig *config, FwSession *session, int32_t pressureHpa)
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
        ok = fwControllerAdaptiveCalibrate(config, session, pressureHpa, errorHpa, elapsedMs);
        ok = fwControllerAdaptiveApply(session, 0L, nowMs) && ok;
        g_adaptiveLastMs = nowMs;
        return fwControllerApplyOutputs(session) && ok;
    }
    dutyX1000 = fwControllerAdaptivePid(errorHpa, elapsedMs);
    ok = fwControllerAdaptiveCalibrate(config, session, pressureHpa, errorHpa, elapsedMs);
    ok = fwControllerAdaptiveApply(session, dutyX1000, nowMs) && ok;
    g_adaptiveLastMs = nowMs;
    return fwControllerApplyOutputs(session) && ok;
}

static bool fwControllerApplyOutputs(const FwSession *session)
{
    bool ok = false;
    assert(session != nullptr);
    assert(g_pumpDutyX1000 >= 0L);
    assert(g_pumpDutyX1000 <= 1000L);
    if (session == nullptr) {
        return false;
    }
    ok = sysOutputsSetPumpDutyX1000(session->pumpActive ? g_pumpDutyX1000 : 0L);
    ok = sysOutputsSetValve(session->valveActive) && ok;
    return ok;
}

static bool fwControllerShutdown(FwSession *session, const char *reason)
{
    assert(session != nullptr);
    assert(reason != nullptr);
    if ((session == nullptr) || (reason == nullptr)) {
        return false;
    }
    Serial.printf("CTRL: shutdown, reason=%s\n", reason);
    session->pumpActive = false;
    session->valveActive = false;
    session->temporaryPumpDisabled = false;
    g_pumpDutyX1000 = 0L;
    return fwControllerApplyOutputs(session);
}

bool fwControllerSetPump(FwSession *session, bool active, bool manual)
{
    assert(session != nullptr);
    assert((active == true) || (active == false));
    if (session == nullptr) {
        return false;
    }
    session->pumpActive = active;
    g_pumpDutyX1000 = active ? 1000L : 0L;
    if (manual && !active) {
        session->temporaryPumpDisabled = true;
    }
    if (manual && active) {
        session->temporaryPumpDisabled = false;
    }
    Serial.printf("CTRL: pump=%u manual=%u temporary_disabled=%u\n",
        active ? 1U : 0U, manual ? 1U : 0U, session->temporaryPumpDisabled ? 1U : 0U);
    return fwControllerApplyOutputs(session);
}

bool fwControllerSetValve(FwSession *session, bool active)
{
    assert(session != nullptr);
    assert((active == true) || (active == false));
    if (session == nullptr) {
        return false;
    }
    session->valveActive = active;
    Serial.printf("CTRL: valve=%u\n", active ? 1U : 0U);
    return fwControllerApplyOutputs(session);
}

static bool fwControllerUpdatePressureStats(FwSession *session, int32_t pressureHpa)
{
    bool active = false;
    assert(session != nullptr);
    assert(pressureHpa > -2000L);
    if (session == nullptr) {
        return false;
    }
    session->currentPressureHpa = pressureHpa;
    active = session->pumpActive || session->valveActive;
    if (!active) {
        session->minPressureHpa = pressureHpa;
        session->maxPressureHpa = pressureHpa;
        return true;
    }
    if (pressureHpa < session->minPressureHpa) {
        session->minPressureHpa = pressureHpa;
    }
    if (pressureHpa > session->maxPressureHpa) {
        session->maxPressureHpa = pressureHpa;
    }
    return true;
}

static bool fwControllerPressureReached(const FwConfig *config, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(config->pressureDeadzoneHpa >= 0L);
    if (config == nullptr) {
        return false;
    }
    if (config->targetPressureHpa < 0L) {
        return pressureHpa <= config->targetPressureHpa;
    }
    return pressureHpa >= config->targetPressureHpa;
}

static bool fwControllerPressureBelowRestart(const FwConfig *config, int32_t pressureHpa)
{
    int32_t restartPressure = 0L;
    assert(config != nullptr);
    assert(config->pressureDeadzoneHpa >= 0L);
    if (config == nullptr) {
        return false;
    }
    if (config->targetPressureHpa < 0L) {
        restartPressure = config->targetPressureHpa + config->pressureDeadzoneHpa;
        return pressureHpa > restartPressure;
    }
    restartPressure = config->targetPressureHpa - config->pressureDeadzoneHpa;
    return pressureHpa < restartPressure;
}

static bool fwControllerAutomatic(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    bool reached = false;
    bool restart = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if (config->mode == FW_MODE_TARGET_ADAPTIVE) {
        return fwControllerAdaptiveKeep(config, session, pressureHpa);
    }
    g_pumpDutyX1000 = session->pumpActive ? 1000L : 0L;
    reached = fwControllerPressureReached(config, pressureHpa);
    if (reached && session->pumpActive) {
        session->pumpActive = false;
        session->currentPumpingSeconds = 0UL;
        Serial.printf("CTRL: target reached pressure_hPa=%ld\n", pressureHpa);
    }
    restart = fwControllerPressureBelowRestart(config, pressureHpa);
    if ((config->mode == FW_MODE_TARGET_HYSTERESIS) && restart && !session->temporaryPumpDisabled) {
        if (!session->pumpActive) {
            session->currentPumpingSeconds = 0UL;
            Serial.printf("CTRL: target_hysteresis restart pressure_hPa=%ld target_hPa=%ld deadzone_hPa=%ld\n",
                pressureHpa, config->targetPressureHpa, config->pressureDeadzoneHpa);
        }
        session->pumpActive = true;
    }
    return fwControllerApplyOutputs(session);
}

static bool fwControllerTickSecond(const FwConfig *config, FwSession *session)
{
    bool active = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    active = session->pumpActive || session->valveActive;
    if (!active && (session->currentSessionSeconds > 0UL)) {
        session->lastSessionSeconds = session->currentSessionSeconds;
        session->currentSessionSeconds = 0UL;
        session->currentPumpingSeconds = 0UL;
        return true;
    }
    if (!active) {
        return true;
    }
    session->currentSessionSeconds++;
    session->totalSessionSeconds++;
    if (session->pumpActive) {
        session->currentPumpingSeconds++;
    }
    if (session->currentSessionSeconds >= config->maxSessionSeconds) {
        return fwControllerShutdown(session, "max session time");
    }
    if (session->currentPumpingSeconds >= config->maxPumpingSeconds) {
        return fwControllerSetPump(session, false, false);
    }
    return true;
}

static bool fwControllerUpdateTime(const FwConfig *config, FwSession *session)
{
    uint32_t nowMs = millis();
    uint32_t elapsedMs = 0UL;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    elapsedMs = nowMs - g_lastTickMs;
    g_lastTickMs = nowMs;
    g_secondAccumulatorMs += elapsedMs;
    if (g_secondAccumulatorMs >= 1000UL) {
        g_secondAccumulatorMs -= 1000UL;
        return fwControllerTickSecond(config, session);
    }
    return true;
}

static bool fwControllerRemotePumpActive(const FwConfig *config, const FwSession *session)
{
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if ((config->mode == FW_MODE_TARGET_HYSTERESIS) || (config->mode == FW_MODE_TARGET_ADAPTIVE)) {
        return !session->temporaryPumpDisabled;
    }
    return session->pumpActive;
}

bool fwControllerHandleRf(FwConfig *config, FwSession *session, SysRfButton button)
{
    bool ok = true;
    bool remotePumpActive = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if ((button == SYS_RF_BUTTON_A) && !config->pumpRemoteDisabled) {
        remotePumpActive = fwControllerRemotePumpActive(config, session);
        if (remotePumpActive) {
            ok = fwControllerSetPump(session, false, true);
            Serial.println("RF: button A accepted, manual pump suspend");
            return ok;
        }
        if (config->mode == FW_MODE_MANUAL) {
            session->valveActive = true;
            Serial.println("RF: button A accepted, pump start and valve lock");
        } else {
            Serial.println("RF: button A accepted, pump start");
        }
        ok = fwControllerSetPump(session, true, true);
    }
    if ((button == SYS_RF_BUTTON_B) && !config->valveRemoteDisabled) {
        ok = fwControllerSetValve(session, !session->valveActive) && ok;
        Serial.println("RF: button B accepted, valve toggle");
    }
    if ((button != SYS_RF_BUTTON_NONE) && !ok) {
        Serial.println("RF: command output apply failed");
    }
    return ok;
}

bool fwControllerRunCalibration(FwConfig *config, FwSession *session)
{
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    session->pumpActive = false;
    session->valveActive = false;
    ok = fwControllerApplyOutputs(session);
    if (!ok) {
        return false;
    }
    delay(250);
    ok = sysPressureCalibrate(config);
    config->calibrationActive = false;
    return ok;
}

bool fwControllerInit(FwConfig *config, FwSession *session)
{
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    g_lastTickMs = millis();
    if (!config->calibrated) {
        Serial.println("CTRL: no calibration found, auto-calibrating with valve open");
        ok = fwControllerRunCalibration(config, session);
        if (!ok) {
            Serial.println("CTRL: auto-calibration failed, using default divider");
        }
    }
    return fwControllerApplyOutputs(session);
}

bool fwControllerPoll(FwConfig *config, FwSession *session)
{
    bool ok = false;
    int32_t pressureHpa = 0L;
    int32_t adcMv = 0L;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = sysPressureReadHpa(config, &pressureHpa, &adcMv);
    if (!ok) {
        return ok; // Invalid pressure read
    }
    ok = fwControllerUpdatePressureStats(session, pressureHpa);
    if (ok && (config->mode != FW_MODE_MANUAL)) {
        ok = fwControllerAutomatic(config, session, pressureHpa);
    }
    if (ok && config->calibrationActive) {
        ok = fwControllerRunCalibration(config, session);
    }
    ok = fwControllerUpdateTime(config, session) && ok;
    return ok;
}
