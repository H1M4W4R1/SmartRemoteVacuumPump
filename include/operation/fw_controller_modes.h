#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool fwControllerManualRemotePumpActive(const FwSession *session);
bool fwControllerTargetOneshotPoll(FwConfig *config, FwSession *session, int32_t pressureHpa);
bool fwControllerTargetHysteresisPoll(FwConfig *config, FwSession *session, int32_t pressureHpa);
bool fwControllerTargetAdaptivePoll(FwConfig *config, FwSession *session, int32_t pressureHpa);
