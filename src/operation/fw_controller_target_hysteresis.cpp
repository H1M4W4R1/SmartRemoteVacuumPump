#include <operation/fw_controller_modes.h>

#include <assert.h>

#include <operation/fw_controller_common.h>

bool fwControllerTargetHysteresisPoll(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    bool ok = false;
    bool reached = false;
    bool restart = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = fwControllerCommonSetPumpDutyX1000(session->pumpActive ? 1000L : 0L);
    if (!ok) {
        return false;
    }
    reached = fwControllerCommonPressureReached(config, pressureHpa);
    if (reached && session->pumpActive) {
        session->pumpActive = false;
        session->currentPumpingSeconds = 0UL;
        Serial.printf("CTRL: target reached pressure_hPa=%ld\n", pressureHpa);
    }
    restart = fwControllerCommonPressureBelowRestart(config, pressureHpa);
    if (restart && !session->temporaryPumpDisabled) {
        if (!session->pumpActive) {
            session->currentPumpingSeconds = 0UL;
            Serial.printf("CTRL: target_hysteresis restart pressure_hPa=%ld target_hPa=%ld deadzone_hPa=%ld\n",
                pressureHpa, config->targetPressureHpa, config->pressureDeadzoneHpa);
        }
        session->pumpActive = true;
    }
    return fwControllerCommonApplyOutputs(session);
}
