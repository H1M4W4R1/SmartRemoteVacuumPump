#include <operation/fw_controller_common.h>

#include <assert.h>

#include <systems/sys_outputs.h>

static int32_t g_pumpDutyX1000 = 1000L;

bool fwControllerCommonSetPumpDutyX1000(int32_t dutyX1000)
{
    assert(dutyX1000 >= 0L);
    assert(dutyX1000 <= 1000L);
    if ((dutyX1000 < 0L) || (dutyX1000 > 1000L)) {
        return false;
    }
    g_pumpDutyX1000 = dutyX1000;
    return true;
}

int32_t fwControllerCommonPumpDutyX1000(void)
{
    assert(g_pumpDutyX1000 >= 0L);
    assert(g_pumpDutyX1000 <= 1000L);
    return g_pumpDutyX1000;
}

bool fwControllerCommonApplyOutputs(const FwSession *session)
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

bool fwControllerCommonHoldIfValveUnlocked(FwSession *session)
{
    bool ok = false;
    assert(session != nullptr);
    assert(fwControllerCommonPumpDutyX1000() >= 0L);
    if (session == nullptr) {
        return false;
    }
    if (session->valveActive) {
        return true;
    }
    if (session->pumpActive) {
        Serial.println("CTRL: pump hold, valve unlocked");
    }
    session->pumpActive = false;
    session->currentPumpingSeconds = 0UL;
    ok = fwControllerCommonSetPumpDutyX1000(0L);
    return fwControllerCommonApplyOutputs(session) && ok;
}

bool fwControllerCommonShutdown(FwSession *session, const char *reason)
{
    bool ok = false;
    assert(session != nullptr);
    assert(reason != nullptr);
    if ((session == nullptr) || (reason == nullptr)) {
        return false;
    }
    Serial.printf("CTRL: shutdown, reason=%s\n", reason);
    session->pumpActive = false;
    session->valveActive = false;
    session->temporaryPumpDisabled = false;
    ok = fwControllerCommonSetPumpDutyX1000(0L);
    return fwControllerCommonApplyOutputs(session) && ok;
}

int32_t fwControllerCommonClampInt32(int32_t value, int32_t minimum, int32_t maximum)
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

bool fwControllerCommonPressureReached(const FwConfig *config, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(pressureHpa > -2000L);
    if (config == nullptr) {
        return false;
    }
    return fwControllerCommonPressureReachedAt(config, pressureHpa, config->targetPressureHpa);
}

bool fwControllerCommonPressureBelowRestart(const FwConfig *config, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(pressureHpa < 2000L);
    if (config == nullptr) {
        return false;
    }
    return fwControllerCommonPressureBelowRestartAt(config, pressureHpa, config->targetPressureHpa);
}

bool fwControllerCommonPressureReachedAt(const FwConfig *config, int32_t pressureHpa, int32_t targetHpa)
{
    assert(config != nullptr);
    assert(pressureHpa > -2000L);
    if (config == nullptr) {
        return false;
    }
    assert(config->pressureDeadzoneHpa >= 0L);
    if (targetHpa < 0L) {
        return pressureHpa <= targetHpa;
    }
    return pressureHpa >= targetHpa;
}

bool fwControllerCommonPressureBelowRestartAt(const FwConfig *config, int32_t pressureHpa, int32_t targetHpa)
{
    int32_t restartPressure = 0L;
    assert(config != nullptr);
    assert(pressureHpa < 2000L);
    if (config == nullptr) {
        return false;
    }
    assert(config->pressureDeadzoneHpa >= 0L);
    if (targetHpa < 0L) {
        restartPressure = targetHpa + config->pressureDeadzoneHpa;
        return pressureHpa > restartPressure;
    }
    restartPressure = targetHpa - config->pressureDeadzoneHpa;
    return pressureHpa < restartPressure;
}
