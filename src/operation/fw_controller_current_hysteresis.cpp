#include <operation/fw_controller_modes.h>

#include <assert.h>

#include <operation/fw_controller_common.h>

bool fwControllerCurrentHysteresisPoll(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    bool ok = false;
    bool reached = false;
    bool restart = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = fwControllerCommonHoldIfValveUnlocked(session);
    if (!ok || !session->valveActive) {
        return ok;
    }
    if (!session->currentHoldPressureValid) {
        session->pumpActive = false;
        ok = fwControllerCommonSetPumpDutyX1000(0L);
        return fwControllerCommonApplyOutputs(session) && ok;
    }
    ok = fwControllerCommonSetPumpDutyX1000(session->pumpActive ? 1000L : 0L);
    if (!ok) {
        return false;
    }
    reached = fwControllerCommonPressureReachedAt(config, pressureHpa, session->currentHoldPressureHpa);
    if (reached && session->pumpActive) {
        session->pumpActive = false;
        session->currentPumpingSeconds = 0UL;
        Serial.printf("CTRL: current target reached pressure_hPa=%ld\n", pressureHpa);
    }
    restart = fwControllerCommonPressureBelowRestartAt(config, pressureHpa, session->currentHoldPressureHpa);
    if (restart) {
        if (!session->pumpActive) {
            session->currentPumpingSeconds = 0UL;
            Serial.printf("CTRL: current_hysteresis restart pressure_hPa=%ld hold_hPa=%ld deadzone_hPa=%ld\n",
                pressureHpa, session->currentHoldPressureHpa, config->pressureDeadzoneHpa);
        }
        session->pumpActive = true;
    }
    return fwControllerCommonApplyOutputs(session);
}
