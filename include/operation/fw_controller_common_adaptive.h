#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool fwControllerCommonAdaptiveTargetPoll(FwConfig *config, FwSession *session, int32_t pressureHpa);
bool fwControllerCommonAdaptiveCurrentPoll(FwConfig *config, FwSession *session, int32_t pressureHpa);
