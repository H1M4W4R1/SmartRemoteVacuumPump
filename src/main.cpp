#include <Arduino.h>

#include <assert.h>

#include <operation/fw_config.h>
#include <operation/fw_controller.h>
#include <operation/fw_state.h>
#include <systems/sys_ble.h>
#include <systems/sys_ota.h>
#include <systems/sys_outputs.h>
#include <systems/sys_pressure.h>
#include <systems/sys_rf.h>
#include <systems/sys_watchdog.h>

static FwConfig g_config;
static FwSession g_session;
static bool g_bleActive = true;

static bool appInit(void)
{
    bool ok = true;
    assert(FW_LOOP_PERIOD_MS > 0UL);
    assert(strlen(FW_DEVICE_NAME) > 0U);
    ok = sysWatchdogInit() && ok;
    ok = fwStateInit(&g_config, &g_session) && ok;
    ok = sysOutputsInit() && ok;
    ok = sysPressureInit() && ok;
    ok = sysRfInit() && ok;
    ok = fwControllerInit(&g_config, &g_session) && ok;
    ok = sysBleInit(&g_config, &g_session) && ok;
    return ok;
}

void setup()
{
    bool ok = false;
    Serial.begin(115200);
    delay(250);
    Serial.printf("\n%s firmware boot, author=H1M4W4R1\n", FW_DEVICE_NAME);
    ok = appInit();
    if (!ok) {
        Serial.println("APP: init failed, outputs forced inactive");
        (void)sysOutputsSetPump(false);
        (void)sysOutputsSetValve(false);
    }
}

void loop()
{
    bool ok = false;
    SysRfButton button = SYS_RF_BUTTON_NONE;
    ok = sysRfPoll(&button);
    if (ok && (button != SYS_RF_BUTTON_NONE)) {
        ok = fwControllerHandleRf(&g_config, &g_session, button);
    }
    ok = fwControllerPoll(&g_config, &g_session) && ok;
    ok = fwConfigSaveIfPending(&g_config) && ok;
    if (g_config.otaActive && g_bleActive) {
        ok = sysBleStop() && ok;
        g_bleActive = false;
    }
    if (g_config.otaActive) {
        ok = sysOtaPoll(&g_config) && ok;
    } else if (g_bleActive) {
        ok = sysBlePoll(&g_config, &g_session) && ok;
    }
    if (!ok) {
        Serial.println("APP: loop warning, one subsystem returned false");
    }
    delay(FW_LOOP_PERIOD_MS);
}
