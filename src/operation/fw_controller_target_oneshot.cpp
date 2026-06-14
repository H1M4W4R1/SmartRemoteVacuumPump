#include <operation/fw_controller_modes.h>

#include <assert.h>

#include <operation/fw_controller_common.h>

bool fwControllerTargetOneshotPoll(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    bool ok = false;
    bool reached = false;
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
    return fwControllerCommonApplyOutputs(session);
}
