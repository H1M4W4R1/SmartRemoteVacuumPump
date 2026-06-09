#pragma once

#include <Arduino.h>

enum FwMode {
    FW_MODE_MANUAL = 0,
    FW_MODE_AUTOMATIC_KEEP = 1,
    FW_MODE_AUTOMATIC_SINGLE = 2,
    FW_MODE_ADAPTIVE_KEEP = 3
};

struct FwConfig {
    uint32_t maxSessionSeconds;
    uint32_t maxPumpingSeconds;
    FwMode mode;
    bool pumpRemoteDisabled;
    bool valveRemoteDisabled;
    int32_t targetPressureHpa;
    bool defaultOn;
    bool calibrationActive;
    int32_t pressureDeadzoneHpa;
    int32_t dividerMultiplierX1000;
    bool calibrated;
    bool savePending;
};

struct FwSession {
    bool pumpActive;
    bool valveActive;
    bool temporaryPumpDisabled;
    uint32_t currentSessionSeconds;
    uint32_t currentPumpingSeconds;
    uint32_t lastSessionSeconds;
    uint32_t totalSessionSeconds;
    int32_t currentPressureHpa;
    int32_t minPressureHpa;
    int32_t maxPressureHpa;
};

bool fwStateInit(FwConfig *config, FwSession *session);
bool fwConfigLoad(FwConfig *config);
bool fwConfigSave(const FwConfig *config);
bool fwConfigRequestSave(FwConfig *config);
bool fwConfigSaveIfPending(FwConfig *config);
bool fwPressureCalibrationSave(FwConfig *config, int32_t dividerMultiplierX1000);
const char *fwModeToText(FwMode mode);
bool fwModeFromText(const String &text, FwMode *mode);
