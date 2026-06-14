#include <operation/fw_controller.h>

#include <assert.h>

#include <operation/fw_controller_common.h>
#include <operation/fw_controller_modes.h>
#include <systems/sys_pressure.h>

static uint32_t g_lastTickMs = 0UL;
static uint32_t g_secondAccumulatorMs = 0UL;

static bool fwControllerModeIsCurrent(FwMode mode)
{
    assert(mode >= FW_MODE_MANUAL);
    assert(mode <= FW_MODE_CURRENT_ADAPTIVE);
    return (mode == FW_MODE_CURRENT_HYSTERESIS) || (mode == FW_MODE_CURRENT_ADAPTIVE);
}

static bool fwControllerModeIsVirtualActive(FwMode mode)
{
    assert(mode >= FW_MODE_MANUAL);
    assert(mode <= FW_MODE_CURRENT_ADAPTIVE);
    return (mode == FW_MODE_TARGET_HYSTERESIS) || (mode == FW_MODE_TARGET_ADAPTIVE);
}

bool fwControllerCaptureCurrentPressure(FwSession *session)
{
    assert(session != nullptr);
    assert((session == nullptr) || (session->currentPressureHpa > -2000L));
    if (session == nullptr) {
        return false;
    }
    session->currentHoldPressureHpa = session->currentPressureHpa;
    session->currentHoldPressureValid = true;
    Serial.printf("CTRL: current hold captured pressure_hPa=%ld\n", session->currentHoldPressureHpa);
    return true;
}

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

bool fwControllerSetPumpCommand(const FwConfig *config, FwSession *session, bool active)
{
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if (fwControllerModeIsCurrent(config->mode) && !active) {
        ok = fwControllerCaptureCurrentPressure(session);
        ok = fwControllerSetPump(session, false, false) && ok;
        return ok;
    }
    if ((config->mode != FW_MODE_MANUAL) && active && !session->valveActive) {
        Serial.println("CTRL: pump start blocked, valve unlocked");
        return fwControllerSetPump(session, false, false);
    }
    return fwControllerSetPump(session, active, true);
}

bool fwControllerSetValve(FwSession *session, bool active)
{
    bool ok = false;
    assert(session != nullptr);
    assert((active == true) || (active == false));
    if (session == nullptr) {
        return false;
    }
    session->valveActive = active;
    if (!active && session->pumpActive) {
        session->pumpActive = false;
        session->currentPumpingSeconds = 0UL;
        ok = fwControllerCommonSetPumpDutyX1000(0L);
        if (!ok) {
            return false;
        }
        Serial.println("CTRL: pump hold, valve unlocked");
    }
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
    if (config->mode == FW_MODE_CURRENT_ADAPTIVE) {
        return fwControllerCurrentAdaptivePoll(config, session, pressureHpa);
    }
    if (config->mode == FW_MODE_TARGET_HYSTERESIS) {
        return fwControllerTargetHysteresisPoll(config, session, pressureHpa);
    }
    if (config->mode == FW_MODE_CURRENT_HYSTERESIS) {
        return fwControllerCurrentHysteresisPoll(config, session, pressureHpa);
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
    if (fwControllerModeIsVirtualActive(config->mode)) {
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
            ok = fwControllerSetPumpCommand(config, session, false);
            Serial.println("RF: button A accepted, manual pump suspend");
            return ok;
        }
        if (config->mode == FW_MODE_MANUAL) {
            session->valveActive = true;
            Serial.println("RF: button A accepted, pump start and valve lock");
        } else {
            Serial.println("RF: button A accepted, pump start");
        }
        ok = fwControllerSetPumpCommand(config, session, true);
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
