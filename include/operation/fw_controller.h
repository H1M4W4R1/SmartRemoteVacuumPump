#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>
#include <systems/sys_rf.h>

bool fwControllerInit(FwConfig *config, FwSession *session);
bool fwControllerPoll(FwConfig *config, FwSession *session);
bool fwControllerHandleRf(FwConfig *config, FwSession *session, SysRfButton button);
bool fwControllerSetPump(FwSession *session, bool active, bool manual);
bool fwControllerSetValve(FwSession *session, bool active);
bool fwControllerRunCalibration(FwConfig *config, FwSession *session);
