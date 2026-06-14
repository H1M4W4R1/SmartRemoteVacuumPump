#include <operation/fw_controller.h>

#include <assert.h>

#include <operation/fw_controller_common.h>
#include <operation/fw_controller_modes.h>
#include <systems/sys_pressure.h>

static uint32_t g_lastTickMs = 0UL;
static uint32_t g_secondAccumulatorMs = 0UL;

bool fwControllerSetPump(FwSession *session, bool active, bool manual)
{
    bool ok = false;
    assert(session != nullptr);
    assert((active == true) || (active == false));
    if (session == nullptr) {
        return false;
    }
    session->pumpActive = active;
    ok = fwControllerCommonSetPumpDutyX1000(active ? 1000L : 0L);
    if (!ok) {
        return false;
    }
    if (manual && !active) {
        session->temporaryPumpDisabled = true;
    }
    if (manual && active) {
        session->temporaryPumpDisabled = false;
    }
    Serial.printf("CTRL: pump=%u manual=%u temporary_disabled=%u\n",
        active ? 1U : 0U, manual ? 1U : 0U, session->temporaryPumpDisabled ? 1U : 0U);
    return fwControllerCommonApplyOutputs(session);
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
    return fwControllerCommonApplyOutputs(session);
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

static bool fwControllerAutomatic(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if (config->mode == FW_MODE_TARGET_ADAPTIVE) {
        return fwControllerTargetAdaptivePoll(config, session, pressureHpa);
    }
    if (config->mode == FW_MODE_TARGET_HYSTERESIS) {
        return fwControllerTargetHysteresisPoll(config, session, pressureHpa);
    }
    if (config->mode == FW_MODE_TARGET_ONESHOT) {
        return fwControllerTargetOneshotPoll(config, session, pressureHpa);
    }
    return true;
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
        return fwControllerCommonShutdown(session, "max session time");
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
    return fwControllerManualRemotePumpActive(session);
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
    ok = fwControllerCommonApplyOutputs(session);
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
    return fwControllerCommonApplyOutputs(session);
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
