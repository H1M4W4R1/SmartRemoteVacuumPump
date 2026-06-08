#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool sysBleInit(FwConfig *config, FwSession *session);
bool sysBlePoll(const FwConfig *config, const FwSession *session);

