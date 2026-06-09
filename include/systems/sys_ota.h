#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool sysOtaStart(const FwConfig *config);
bool sysOtaPoll(const FwConfig *config);
