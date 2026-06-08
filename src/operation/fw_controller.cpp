#include <operation/fw_controller.h>

#include <assert.h>

#include <systems/sys_outputs.h>
#include <systems/sys_pressure.h>

static uint32_t g_lastTickMs = 0UL;
static uint32_t g_secondAccumulatorMs = 0UL;

static bool fwControllerApplyOutputs(const FwSession *session)
{
    bool ok = false;
    assert(session != nullptr);
    assert(FW_MODE_AUTOMATIC_SINGLE >= FW_MODE_MANUAL);
    if (session == nullptr) {
        return false;
    }
    ok = sysOutputsSetPump(session->pumpActive);
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

static bool fwControllerDirectionValid(const FwConfig *config, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(pressureHpa < 2000L);
    if (config == nullptr) {
        return false;
    }
    if ((config->targetPressureHpa < 0L) && (pressureHpa > 50L)) {
        return false;
    }
    if ((config->targetPressureHpa > 0L) && (pressureHpa < -50L)) {
        return false;
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
    if (!fwControllerDirectionValid(config, pressureHpa)) {
        return fwControllerShutdown(session, "pressure opposite target direction");
    }
    reached = fwControllerPressureReached(config, pressureHpa);
    if (reached && session->pumpActive) {
        session->pumpActive = false;
        session->currentPumpingSeconds = 0UL;
        Serial.printf("CTRL: target reached pressure_hPa=%ld\n", pressureHpa);
    }
    restart = fwControllerPressureBelowRestart(config, pressureHpa);
    if ((config->mode == FW_MODE_AUTOMATIC_KEEP) && restart && !session->temporaryPumpDisabled) {
        if (!session->pumpActive) {
            session->currentPumpingSeconds = 0UL;
            Serial.printf("CTRL: automatic_keep restart pressure_hPa=%ld target_hPa=%ld deadzone_hPa=%ld\n",
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

bool fwControllerHandleRf(FwConfig *config, FwSession *session, SysRfButton button)
{
    bool ok = true;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if ((button == SYS_RF_BUTTON_A) && !config->pumpRemoteDisabled) {
        if (session->pumpActive) {
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
        return fwControllerShutdown(session, "pressure read invalid");
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
