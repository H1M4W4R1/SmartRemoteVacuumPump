#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool sysPressureInit(void);
bool sysPressureReadHpa(const FwConfig *config, int32_t *pressureHpa, int32_t *adcMillivolts);
bool sysPressureCalibrate(FwConfig *config);
