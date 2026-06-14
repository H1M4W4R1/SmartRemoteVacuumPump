#include <operation/fw_controller_modes.h>

#include <assert.h>

bool fwControllerManualRemotePumpActive(const FwSession *session)
{
    assert(session != nullptr);
    assert((session == nullptr) || (session->pumpActive == true) || (session->pumpActive == false));
    if (session == nullptr) {
        return false;
    }
    return session->pumpActive;
}
