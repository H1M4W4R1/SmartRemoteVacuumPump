#include <operation/fw_state.h>

#include <Preferences.h>
#include <assert.h>

#include <operation/fw_config.h>

static Preferences g_prefs;

static bool fwConfigApplyDefaults(FwConfig *config)
{
    assert(config != nullptr);
    assert(FW_DEFAULT_MAX_SESSION_SECONDS > 0UL);
    if (config == nullptr) {
        return false;
    }
    config->maxSessionSeconds = FW_DEFAULT_MAX_SESSION_SECONDS;
    config->maxPumpingSeconds = FW_DEFAULT_MAX_PUMPING_SECONDS;
    config->mode = FW_MODE_MANUAL;
    config->pumpRemoteDisabled = false;
    config->valveRemoteDisabled = false;
    config->targetPressureHpa = -500L;
    config->defaultOn = false;
    config->calibrationActive = false;
    config->otaActive = false;
    config->pressureDeadzoneHpa = FW_DEFAULT_PRESSURE_DEADZONE_HPA;
    config->dividerMultiplierX1000 = FW_DEFAULT_DIVIDER_MULTIPLIER_X1000;
    config->calibrated = false;
    config->savePending = false;
    return true;
}

bool fwStateInit(FwConfig *config, FwSession *session)
{
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = fwConfigLoad(config);
    if (!ok) {
        return false;
    }
    session->pumpActive = config->defaultOn;
    session->valveActive = false;
    session->temporaryPumpDisabled = false;
    session->currentSessionSeconds = 0UL;
    session->currentPumpingSeconds = 0UL;
    session->lastSessionSeconds = 0UL;
    session->totalSessionSeconds = 0UL;
    session->currentPressureHpa = 0L;
    session->minPressureHpa = 0L;
    session->maxPressureHpa = 0L;
    return true;
}

bool fwConfigLoad(FwConfig *config)
{
    bool ok = false;
    assert(config != nullptr);
    assert(FW_DEFAULT_PRESSURE_DEADZONE_HPA > 0L);
    if (config == nullptr) {
        return false;
    }
    ok = fwConfigApplyDefaults(config);
    if (!ok) {
        return false;
    }
    ok = g_prefs.begin("ost-rsp", true);
    if (!ok) {
        Serial.println("NVS: read namespace open failed, using defaults");
        return true;
    }
    config->defaultOn = g_prefs.getBool("default_on", config->defaultOn);
    config->pressureDeadzoneHpa = g_prefs.getInt("deadzone", config->pressureDeadzoneHpa);
    config->dividerMultiplierX1000 = g_prefs.getInt("div_x1000", config->dividerMultiplierX1000);
    config->calibrated = g_prefs.getBool("cal_ok", false);
    config->savePending = false;
    g_prefs.end();
    return true;
}

bool fwConfigSave(const FwConfig *config)
{
    bool ok = false;
    assert(config != nullptr);
    assert(FW_DEFAULT_MAX_PUMPING_SECONDS > 0UL);
    if (config == nullptr) {
        return false;
    }
    ok = g_prefs.begin("ost-rsp", false);
    if (!ok) {
        Serial.println("NVS: write namespace open failed");
        return false;
    }
    ok = g_prefs.putBool("default_on", config->defaultOn) > 0U;
    ok = (g_prefs.putInt("deadzone", config->pressureDeadzoneHpa) > 0U) && ok;
    ok = (g_prefs.putInt("div_x1000", config->dividerMultiplierX1000) > 0U) && ok;
    ok = (g_prefs.putBool("cal_ok", config->calibrated) > 0U) && ok;
    g_prefs.end();
    return ok;
}

bool fwConfigRequestSave(FwConfig *config)
{
    assert(config != nullptr);
    assert(FW_DEFAULT_PRESSURE_DEADZONE_HPA > 0L);
    if (config == nullptr) {
        return false;
    }
    config->savePending = true;
    return true;
}

bool fwConfigSaveIfPending(FwConfig *config)
{
    bool ok = false;
    assert(config != nullptr);
    assert(FW_DEFAULT_MAX_SESSION_SECONDS > 0UL);
    if (config == nullptr) {
        return false;
    }
    if (!config->savePending) {
        return true;
    }
    ok = fwConfigSave(config);
    if (ok) {
        config->savePending = false;
        Serial.println("NVS: pending config saved");
    }
    return ok;
}

bool fwPressureCalibrationSave(FwConfig *config, int32_t dividerMultiplierX1000)
{
    bool ok = false;
    assert(config != nullptr);
    assert(dividerMultiplierX1000 > 0L);
    if ((config == nullptr) || (dividerMultiplierX1000 <= 0L)) {
        return false;
    }
    config->dividerMultiplierX1000 = dividerMultiplierX1000;
    config->calibrated = true;
    ok = fwConfigSave(config);
    return ok;
}

const char *fwModeToText(FwMode mode)
{
    assert(mode >= FW_MODE_MANUAL);
    assert(mode <= FW_MODE_TARGET_ADAPTIVE);
    if (mode == FW_MODE_TARGET_HYSTERESIS) {
        return "target_hysteresis";
    }
    if (mode == FW_MODE_TARGET_ONESHOT) {
        return "target_oneshot";
    }
    if (mode == FW_MODE_TARGET_ADAPTIVE) {
        return "target_adaptive";
    }
    return "manual";
}

bool fwModeFromText(const String &text, FwMode *mode)
{
    assert(mode != nullptr);
    assert(text.length() < 32U);
    if (mode == nullptr) {
        return false;
    }
    if (text == "manual") {
        *mode = FW_MODE_MANUAL;
        return true;
    }
    if (text == "target_hysteresis") {
        *mode = FW_MODE_TARGET_HYSTERESIS;
        return true;
    }
    if (text == "target_oneshot") {
        *mode = FW_MODE_TARGET_ONESHOT;
        return true;
    }
    if (text == "target_adaptive") {
        *mode = FW_MODE_TARGET_ADAPTIVE;
        return true;
    }
    return false;
}
