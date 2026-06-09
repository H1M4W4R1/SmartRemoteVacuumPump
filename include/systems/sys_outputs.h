#pragma once

#include <Arduino.h>

bool sysOutputsInit(void);
bool sysOutputsSetPump(bool active);
bool sysOutputsSetPumpDutyX1000(int32_t dutyX1000);
bool sysOutputsSetValve(bool active);
