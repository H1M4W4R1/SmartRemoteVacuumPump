#include <systems/sys_outputs.h>

#include <assert.h>

#include <operation/fw_config.h>

bool sysOutputsInit(void)
{
    bool ok = false;
    assert(FW_PIN_PUMP_OUT >= 0);
    assert(FW_PIN_VALVE_OUT >= 0);
    assert(FW_PUMP_PWM_MAX_DUTY > 0UL);
    ok = ledcAttach(FW_PIN_PUMP_OUT, FW_PUMP_PWM_FREQUENCY_HZ, FW_PUMP_PWM_RESOLUTION_BITS);
    if (!ok) {
        return false;
    }
    pinMode(FW_PIN_VALVE_OUT, OUTPUT);
    ledcWrite(FW_PIN_PUMP_OUT, FW_OUTPUT_INACTIVE_LEVEL == LOW ? 0UL : FW_PUMP_PWM_MAX_DUTY);
    digitalWrite(FW_PIN_VALVE_OUT, FW_OUTPUT_INACTIVE_LEVEL);
    return true;
}

bool sysOutputsSetPump(bool active)
{
    assert(FW_PUMP_PWM_MAX_DUTY > 0UL);
    assert((FW_OUTPUT_ACTIVE_LEVEL == HIGH) || (FW_OUTPUT_ACTIVE_LEVEL == LOW));
    return sysOutputsSetPumpDutyX1000(active ? 1000L : 0L);
}

bool sysOutputsSetPumpDutyX1000(int32_t dutyX1000)
{
    uint32_t pwmDuty = 0UL;
    assert(dutyX1000 >= 0L);
    assert(dutyX1000 <= 1000L);
    if ((dutyX1000 < 0L) || (dutyX1000 > 1000L)) {
        return false;
    }
    pwmDuty = ((uint32_t)dutyX1000 * FW_PUMP_PWM_MAX_DUTY) / 1000UL;
    if (FW_OUTPUT_ACTIVE_LEVEL == LOW) {
        pwmDuty = FW_PUMP_PWM_MAX_DUTY - pwmDuty;
    }
    ledcWrite(FW_PIN_PUMP_OUT, pwmDuty);
    return true;
}

bool sysOutputsSetValve(bool active)
{
    assert(FW_PIN_VALVE_OUT >= 0);
    assert((FW_OUTPUT_INACTIVE_LEVEL == HIGH) || (FW_OUTPUT_INACTIVE_LEVEL == LOW));
    digitalWrite(FW_PIN_VALVE_OUT, active ? FW_OUTPUT_ACTIVE_LEVEL : FW_OUTPUT_INACTIVE_LEVEL);
    return true;
}
