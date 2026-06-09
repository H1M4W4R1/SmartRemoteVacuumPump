#include <systems/sys_ota.h>

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <assert.h>

#include <operation/fw_config.h>

static bool g_otaStarted = false;

static bool sysOtaConfigureAp(void)
{
    bool ok = false;
    assert(strlen(FW_DEVICE_NAME) > 0U);
    assert(strlen(FW_OTA_AP_PASSWORD) >= 8U);
    WiFi.mode(WIFI_AP);
    ok = WiFi.softAP(FW_DEVICE_NAME, FW_OTA_AP_PASSWORD);
    if (!ok) {
        Serial.println("OTA: WiFi AP start failed");
        return false;
    }
    Serial.printf("OTA: AP started ssid=%s ip=%s\n",
        FW_DEVICE_NAME, WiFi.softAPIP().toString().c_str());
    return true;
}

bool sysOtaStart(const FwConfig *config)
{
    bool ok = false;
    assert(config != nullptr);
    assert(FW_OTA_POLL_DELAY_MS > 0UL);
    if (config == nullptr) {
        return false;
    }
    if (!config->otaActive) {
        return true;
    }
    if (g_otaStarted) {
        return true;
    }
    ok = sysOtaConfigureAp();
    if (!ok) {
        return false;
    }
    ArduinoOTA.setHostname(FW_DEVICE_NAME);
    ArduinoOTA.begin();
    g_otaStarted = true;
    Serial.println("OTA: ready, no OTA password configured");
    return true;
}

bool sysOtaPoll(const FwConfig *config)
{
    assert(config != nullptr);
    assert(FW_OTA_POLL_DELAY_MS > 0UL);
    if (config == nullptr) {
        return false;
    }
    if (!config->otaActive) {
        return true;
    }
    if (!g_otaStarted && !sysOtaStart(config)) {
        return false;
    }
    ArduinoOTA.handle();
    delay(FW_OTA_POLL_DELAY_MS);
    return true;
}
