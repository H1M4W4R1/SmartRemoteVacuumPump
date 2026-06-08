#include <systems/sys_outputs.h>

#include <assert.h>

#include <operation/fw_config.h>

bool sysOutputsInit(void)
{
    assert(FW_PIN_PUMP_OUT >= 0);
    assert(FW_PIN_VALVE_OUT >= 0);
    pinMode(FW_PIN_PUMP_OUT, OUTPUT);
    pinMode(FW_PIN_VALVE_OUT, OUTPUT);
    digitalWrite(FW_PIN_PUMP_OUT, FW_OUTPUT_INACTIVE_LEVEL);
    digitalWrite(FW_PIN_VALVE_OUT, FW_OUTPUT_INACTIVE_LEVEL);
    return true;
}

bool sysOutputsSetPump(bool active)
{
    assert(FW_PIN_PUMP_OUT >= 0);
    assert((FW_OUTPUT_ACTIVE_LEVEL == HIGH) || (FW_OUTPUT_ACTIVE_LEVEL == LOW));
    digitalWrite(FW_PIN_PUMP_OUT, active ? FW_OUTPUT_ACTIVE_LEVEL : FW_OUTPUT_INACTIVE_LEVEL);
    return true;
}

bool sysOutputsSetValve(bool active)
{
    assert(FW_PIN_VALVE_OUT >= 0);
    assert((FW_OUTPUT_INACTIVE_LEVEL == HIGH) || (FW_OUTPUT_INACTIVE_LEVEL == LOW));
    digitalWrite(FW_PIN_VALVE_OUT, active ? FW_OUTPUT_ACTIVE_LEVEL : FW_OUTPUT_INACTIVE_LEVEL);
    return true;
}
