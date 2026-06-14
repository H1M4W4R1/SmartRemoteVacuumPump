#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool fwControllerCommonApplyOutputs(const FwSession *session);
bool fwControllerCommonShutdown(FwSession *session, const char *reason);
bool fwControllerCommonSetPumpDutyX1000(int32_t dutyX1000);
int32_t fwControllerCommonPumpDutyX1000(void);
int32_t fwControllerCommonClampInt32(int32_t value, int32_t minimum, int32_t maximum);
bool fwControllerCommonPressureReached(const FwConfig *config, int32_t pressureHpa);
bool fwControllerCommonPressureBelowRestart(const FwConfig *config, int32_t pressureHpa);
