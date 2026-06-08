#pragma once

#include <Arduino.h>

enum SysRfButton {
    SYS_RF_BUTTON_NONE = 0,
    SYS_RF_BUTTON_A = 1,
    SYS_RF_BUTTON_B = 2
};

bool sysRfInit(void);
bool sysRfPoll(SysRfButton *button);

