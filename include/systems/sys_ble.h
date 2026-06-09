#pragma once

#include <Arduino.h>

#include <operation/fw_state.h>

bool sysBleInit(FwConfig *config, FwSession *session);
bool sysBleStop(void);
bool sysBlePoll(const FwConfig *config, const FwSession *session);
