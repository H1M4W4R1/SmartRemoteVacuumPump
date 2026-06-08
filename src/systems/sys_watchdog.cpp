#include <systems/sys_watchdog.h>

#include <assert.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>

#include <operation/fw_config.h>

static uint32_t sysWatchdogIdleMask(void)
{
    uint32_t mask = 0UL;
    assert(portNUM_PROCESSORS > 0);
    assert(portNUM_PROCESSORS <= 2);
    mask = (1UL << portNUM_PROCESSORS) - 1UL;
    return mask;
}

bool sysWatchdogInit(void)
{
    esp_task_wdt_config_t config = {};
    esp_err_t result = ESP_OK;
    assert(FW_TASK_WATCHDOG_TIMEOUT_MS >= 5000UL);
    assert(FW_TASK_WATCHDOG_TIMEOUT_MS <= 60000UL);
    config.timeout_ms = FW_TASK_WATCHDOG_TIMEOUT_MS;
    config.idle_core_mask = sysWatchdogIdleMask();
    config.trigger_panic = true;
    result = esp_task_wdt_reconfigure(&config);
    if (result == ESP_ERR_INVALID_STATE) {
        result = esp_task_wdt_init(&config);
    }
    if (result != ESP_OK) {
        Serial.printf("WDT: configure failed err=%d\n", (int)result);
        return false;
    }
    Serial.printf("WDT: task watchdog timeout=%lu ms\n", FW_TASK_WATCHDOG_TIMEOUT_MS);
    return true;
}
