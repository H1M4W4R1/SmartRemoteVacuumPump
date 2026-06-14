#include <operation/fw_controller_modes.h>

#include <assert.h>

#include <operation/fw_controller_common_adaptive.h>

bool fwControllerCurrentAdaptivePoll(FwConfig *config, FwSession *session, int32_t pressureHpa)
{
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    return fwControllerCommonAdaptiveCurrentPoll(config, session, pressureHpa);
}
