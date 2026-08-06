#include <systems/sys_ble.h>

#include <BluetoothCommandAPI.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <operation/fw_config.h>
#include <operation/fw_controller.h>

static FwConfig *g_config = nullptr;
static FwSession *g_session = nullptr;
static bool g_started = false;

struct BleStatus {
    bool pumpActive;
    bool valveActive;
    uint32_t currentSessionSeconds;
    uint32_t currentPumpingSeconds;
    uint32_t lastSessionSeconds;
    uint32_t totalSessionSeconds;
    int32_t currentPressureHpa;
    int32_t minPressureHpa;
    int32_t maxPressureHpa;
};

static BleStatus g_lastStatus = {};

static bool bleReply(const char *command, const char *value)
{
    const char *data[] = {value};
    bluetooth_command_result_t result = bluetooth_command_result_ok;
    assert(command != nullptr);
    assert(value != nullptr);
    if ((command == nullptr) || (value == nullptr)) {
        return false;
    }
    result = BluetoothCommandAPI::send(command, data, 1U);
    return result == bluetooth_command_result_ok;
}

static bool bleReplyOk(void)
{
    bluetooth_command_result_t result = BluetoothCommandAPI::send("OK");
    assert(g_started);
    return result == bluetooth_command_result_ok;
}

static bool bleReplyError(const char *error)
{
    bool ok = false;
    assert(error != nullptr);
    assert(g_started);
    ok = bleReply("ERR", error);
    return ok;
}

static bool bleReplyInt(const char *command, int32_t value)
{
    char text[16] = {};
    int written = 0;
    assert(command != nullptr);
    assert(g_started);
    written = snprintf(text, sizeof(text), "%ld", (long)value);
    if ((written < 0) || ((size_t)written >= sizeof(text))) {
        return false;
    }
    return bleReply(command, text);
}

static bool bleReplyUint(const char *command, uint32_t value)
{
    char text[16] = {};
    int written = 0;
    assert(command != nullptr);
    assert(g_started);
    written = snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    if ((written < 0) || ((size_t)written >= sizeof(text))) {
        return false;
    }
    return bleReply(command, text);
}

static bool bleReplyBool(const char *command, bool value)
{
    assert(command != nullptr);
    assert(g_started);
    return bleReply(command, value ? "1" : "0");
}

static bool bleNotify(const char *command, const char *value)
{
    const char *data[] = {value};
    bluetooth_command_result_t result = bluetooth_command_result_ok;
    assert(command != nullptr);
    assert(value != nullptr);
    assert(g_started);
    if ((command == nullptr) || (value == nullptr) || !g_started) {
        return false;
    }
    result = BluetoothCommandAPI::notify(command, data, 1U);
    return result == bluetooth_command_result_ok;
}

static bool bleNotifyInt(const char *command, int32_t value)
{
    char text[16] = {};
    int written = 0;
    assert(command != nullptr);
    assert(g_started);
    written = snprintf(text, sizeof(text), "%ld", (long)value);
    if ((written < 0) || ((size_t)written >= sizeof(text))) {
        return false;
    }
    return bleNotify(command, text);
}

static bool bleNotifyUint(const char *command, uint32_t value)
{
    char text[16] = {};
    int written = 0;
    assert(command != nullptr);
    assert(g_started);
    written = snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    if ((written < 0) || ((size_t)written >= sizeof(text))) {
        return false;
    }
    return bleNotify(command, text);
}

static bool bleNotifyBool(const char *command, bool value)
{
    assert(command != nullptr);
    assert(g_started);
    return bleNotify(command, value ? "1" : "0");
}

static bool bleHasNoData(const char *const *data)
{
    assert(data != nullptr);
    assert(g_started);
    return (data != nullptr) && (data[0] == nullptr);
}

static bool bleHasOneData(const char *const *data)
{
    assert(data != nullptr);
    assert(g_started);
    return (data != nullptr) && (data[0] != nullptr) && (data[1] == nullptr);
}

static bool bleParseBool(const char *text, bool *value)
{
    assert(text != nullptr);
    assert(value != nullptr);
    if ((text == nullptr) || (value == nullptr)) {
        return false;
    }
    if (strcmp(text, "0") == 0) {
        *value = false;
        return true;
    }
    if (strcmp(text, "1") == 0) {
        *value = true;
        return true;
    }
    return false;
}

static bool bleParseInt32(const char *text, int32_t *value)
{
    char *end = nullptr;
    long parsed = 0L;
    assert(text != nullptr);
    assert(value != nullptr);
    if ((text == nullptr) || (value == nullptr) || (strlen(text) >= 16U)) {
        return false;
    }
    parsed = strtol(text, &end, 10);
    if ((end == text) || (end == nullptr) || (*end != '\0') || (parsed < INT32_MIN) || (parsed > INT32_MAX)) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static void bleSendError(const char *error)
{
    bool ok = false;
    assert(error != nullptr);
    assert(g_started);
    ok = bleReplyError(error);
    if (!ok) {
        Serial.println("BLE: reply queue full");
    }
}

static void bleSendOk(void)
{
    bool ok = false;
    assert(g_started);
    assert(g_config != nullptr);
    ok = bleReplyOk();
    if (!ok) {
        Serial.println("BLE: reply queue full");
    }
}

static bool bleSetMode(FwMode mode)
{
    bool ok = true;
    assert(g_config != nullptr);
    assert(g_session != nullptr);
    assert(mode <= FW_MODE_CURRENT_ADAPTIVE);
    if ((g_config == nullptr) || (g_session == nullptr) || (mode > FW_MODE_CURRENT_ADAPTIVE)) {
        return false;
    }
    g_config->mode = mode;
    if (((mode == FW_MODE_CURRENT_HYSTERESIS) || (mode == FW_MODE_CURRENT_ADAPTIVE)) && !g_session->pumpActive) {
        ok = fwControllerCaptureCurrentPressure(g_session);
    }
    return ok;
}

static void bleMode(const char *const *data)
{
    FwMode mode = FW_MODE_MANUAL;
    bool ok = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReply("Mode", fwModeToText(g_config->mode));
        return;
    }
    if (!bleHasOneData(data) || !fwModeFromText(data[0], &mode)) {
        bleSendError("invalid_value");
        return;
    }
    ok = bleSetMode(mode);
    if (!ok) {
        bleSendError("operation_failed");
        return;
    }
    bleSendOk();
}

static void blePump(const char *const *data)
{
    bool value = false;
    bool ok = false;
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("Pump", g_session->pumpActive);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    ok = fwControllerSetPumpCommand(g_config, g_session, value);
    if (!ok) {
        bleSendError("operation_failed");
        return;
    }
    bleSendOk();
}

static void bleValve(const char *const *data)
{
    bool value = false;
    bool ok = false;
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("Valve", g_session->valveActive);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    ok = fwControllerSetValve(g_session, value);
    if (!ok) {
        bleSendError("operation_failed");
        return;
    }
    bleSendOk();
}

static void bleCurrentPressure(const char *const *data)
{
    int32_t value = 0L;
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyInt("CurrentPressure", g_session->currentPressureHpa);
        return;
    }
    if (!bleHasOneData(data) || !bleParseInt32(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_session->currentPressureHpa = value;
    bleSendOk();
}

static void bleSessionTime(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyUint("SessionTime", g_session->currentSessionSeconds);
}

static void blePumpTime(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyUint("PumpTime", g_session->currentPumpingSeconds);
}

static void bleLastSessionTime(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyUint("LastSessionTime", g_session->lastSessionSeconds);
}

static void bleTotalSessionTime(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyUint("TotalSessionTime", g_session->totalSessionSeconds);
}

static void bleMinimumPressure(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyInt("MinimumPressure", g_session->minPressureHpa);
}

static void bleMaximumPressure(const char *const *data)
{
    assert(data != nullptr);
    assert(g_session != nullptr);
    if (!bleHasNoData(data)) {
        bleSendError("invalid_arguments");
        return;
    }
    (void)bleReplyInt("MaximumPressure", g_session->maxPressureHpa);
}

static void bleMaxSession(const char *const *data)
{
    int32_t value = 0L;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyUint("MaxSession", g_config->maxSessionSeconds);
        return;
    }
    if (!bleHasOneData(data) || !bleParseInt32(data[0], &value) || (value < 0L)) {
        bleSendError("out_of_range");
        return;
    }
    g_config->maxSessionSeconds = (uint32_t)value;
    bleSendOk();
}

static void bleMaxPumping(const char *const *data)
{
    int32_t value = 0L;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyUint("MaxPumping", g_config->maxPumpingSeconds);
        return;
    }
    if (!bleHasOneData(data) || !bleParseInt32(data[0], &value) || (value < 0L)) {
        bleSendError("out_of_range");
        return;
    }
    g_config->maxPumpingSeconds = (uint32_t)value;
    bleSendOk();
}

static void blePumpRemoteDisabled(const char *const *data)
{
    bool value = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("PumpRemoteDisabled", g_config->pumpRemoteDisabled);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_config->pumpRemoteDisabled = value;
    bleSendOk();
}

static void bleValveRemoteDisabled(const char *const *data)
{
    bool value = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("ValveRemoteDisabled", g_config->valveRemoteDisabled);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_config->valveRemoteDisabled = value;
    bleSendOk();
}

static void bleTargetPressure(const char *const *data)
{
    int32_t value = 0L;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyInt("TargetPressure", g_config->targetPressureHpa);
        return;
    }
    if (!bleHasOneData(data) || !bleParseInt32(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_config->targetPressureHpa = value;
    bleSendOk();
}

static void bleDefaultOn(const char *const *data)
{
    bool value = false;
    bool ok = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("DefaultOn", g_config->defaultOn);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_config->defaultOn = value;
    ok = fwConfigRequestSave(g_config);
    if (!ok) {
        bleSendError("operation_failed");
        return;
    }
    bleSendOk();
}

static void bleCalibrationActive(const char *const *data)
{
    bool value = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyBool("CalibrationActive", g_config->calibrationActive);
        return;
    }
    if (!bleHasOneData(data) || !bleParseBool(data[0], &value)) {
        bleSendError("invalid_value");
        return;
    }
    g_config->calibrationActive = value;
    bleSendOk();
}

static void blePressureDeadzone(const char *const *data)
{
    int32_t value = 0L;
    bool ok = false;
    assert(data != nullptr);
    assert(g_config != nullptr);
    if (bleHasNoData(data)) {
        (void)bleReplyInt("PressureDeadzone", g_config->pressureDeadzoneHpa);
        return;
    }
    if (!bleHasOneData(data) || !bleParseInt32(data[0], &value)
        || (value < FW_MIN_PRESSURE_DEADZONE_HPA) || (value > FW_MAX_PRESSURE_DEADZONE_HPA)) {
        bleSendError("out_of_range");
        return;
    }
    g_config->pressureDeadzoneHpa = value;
    ok = fwConfigRequestSave(g_config);
    if (!ok) {
        bleSendError("operation_failed");
        return;
    }
    bleSendOk();
}

static bool bleRegister(const char *name, bluetooth_command_handler_t handler)
{
    bluetooth_command_result_t result = bluetooth_command_result_ok;
    assert(name != nullptr);
    assert(handler != nullptr);
    if ((name == nullptr) || (handler == nullptr)) {
        return false;
    }
    result = BluetoothCommandAPI::register_command(name, handler);
    return result == bluetooth_command_result_ok;
}

static bool bleRegisterCommands(void)
{
    bool ok = true;
    assert(g_config != nullptr);
    assert(g_session != nullptr);
    ok = bleRegister("Mode", bleMode) && ok;
    ok = bleRegister("Pump", blePump) && ok;
    ok = bleRegister("Valve", bleValve) && ok;
    ok = bleRegister("CurrentPressure", bleCurrentPressure) && ok;
    ok = bleRegister("SessionTime", bleSessionTime) && ok;
    ok = bleRegister("PumpTime", blePumpTime) && ok;
    ok = bleRegister("LastSessionTime", bleLastSessionTime) && ok;
    ok = bleRegister("TotalSessionTime", bleTotalSessionTime) && ok;
    ok = bleRegister("MinimumPressure", bleMinimumPressure) && ok;
    ok = bleRegister("MaximumPressure", bleMaximumPressure) && ok;
    ok = bleRegister("MaxSession", bleMaxSession) && ok;
    ok = bleRegister("MaxPumping", bleMaxPumping) && ok;
    ok = bleRegister("PumpRemoteDisabled", blePumpRemoteDisabled) && ok;
    ok = bleRegister("ValveRemoteDisabled", bleValveRemoteDisabled) && ok;
    ok = bleRegister("TargetPressure", bleTargetPressure) && ok;
    ok = bleRegister("DefaultOn", bleDefaultOn) && ok;
    ok = bleRegister("CalibrationActive", bleCalibrationActive) && ok;
    ok = bleRegister("PressureDeadzone", blePressureDeadzone) && ok;
    return ok;
}

static void bleCaptureStatus(const FwSession *session, BleStatus *status)
{
    assert(session != nullptr);
    assert(status != nullptr);
    if ((session == nullptr) || (status == nullptr)) {
        return;
    }
    status->pumpActive = session->pumpActive;
    status->valveActive = session->valveActive;
    status->currentSessionSeconds = session->currentSessionSeconds;
    status->currentPumpingSeconds = session->currentPumpingSeconds;
    status->lastSessionSeconds = session->lastSessionSeconds;
    status->totalSessionSeconds = session->totalSessionSeconds;
    status->currentPressureHpa = session->currentPressureHpa;
    status->minPressureHpa = session->minPressureHpa;
    status->maxPressureHpa = session->maxPressureHpa;
}

static bool bleStatusChanged(const FwSession *session)
{
    assert(session != nullptr);
    assert(g_started);
    if (session == nullptr) {
        return false;
    }
    return (session->pumpActive != g_lastStatus.pumpActive)
        || (session->valveActive != g_lastStatus.valveActive)
        || (session->currentSessionSeconds != g_lastStatus.currentSessionSeconds)
        || (session->currentPumpingSeconds != g_lastStatus.currentPumpingSeconds)
        || (session->lastSessionSeconds != g_lastStatus.lastSessionSeconds)
        || (session->totalSessionSeconds != g_lastStatus.totalSessionSeconds)
        || (session->currentPressureHpa != g_lastStatus.currentPressureHpa)
        || (session->minPressureHpa != g_lastStatus.minPressureHpa)
        || (session->maxPressureHpa != g_lastStatus.maxPressureHpa);
}

static bool bleNotifyStatusChanges(const FwSession *session)
{
    bool ok = true;
    assert(session != nullptr);
    assert(g_started);
    if (session == nullptr) {
        return false;
    }
    if (!bleStatusChanged(session)) {
        return true;
    }
    if (session->pumpActive != g_lastStatus.pumpActive) {
        ok = bleNotifyBool("Pump", session->pumpActive) && ok;
    }
    if (session->valveActive != g_lastStatus.valveActive) {
        ok = bleNotifyBool("Valve", session->valveActive) && ok;
    }
    if (session->currentSessionSeconds != g_lastStatus.currentSessionSeconds) {
        ok = bleNotifyUint("SessionTime", session->currentSessionSeconds) && ok;
    }
    if (session->currentPumpingSeconds != g_lastStatus.currentPumpingSeconds) {
        ok = bleNotifyUint("PumpTime", session->currentPumpingSeconds) && ok;
    }
    if (session->lastSessionSeconds != g_lastStatus.lastSessionSeconds) {
        ok = bleNotifyUint("LastSessionTime", session->lastSessionSeconds) && ok;
    }
    if (session->totalSessionSeconds != g_lastStatus.totalSessionSeconds) {
        ok = bleNotifyUint("TotalSessionTime", session->totalSessionSeconds) && ok;
    }
    if (session->currentPressureHpa != g_lastStatus.currentPressureHpa) {
        ok = bleNotifyInt("CurrentPressure", session->currentPressureHpa) && ok;
    }
    if (session->minPressureHpa != g_lastStatus.minPressureHpa) {
        ok = bleNotifyInt("MinimumPressure", session->minPressureHpa) && ok;
    }
    if (session->maxPressureHpa != g_lastStatus.maxPressureHpa) {
        ok = bleNotifyInt("MaximumPressure", session->maxPressureHpa) && ok;
    }
    if (ok) {
        bleCaptureStatus(session, &g_lastStatus);
    }
    return ok;
}

bool sysBleInit(FwConfig *config, FwSession *session)
{
    bluetooth_command_result_t result = bluetooth_command_result_ok;
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    g_config = config;
    g_session = session;
    result = BluetoothCommandAPI::begin(FW_DEVICE_NAME);
    if (result != bluetooth_command_result_ok) {
        return false;
    }
    result = BluetoothCommandAPI::set_transmit_rate_hz(20U);
    g_started = result == bluetooth_command_result_ok;
    ok = g_started && bleRegisterCommands();
    if (ok) {
        bleCaptureStatus(session, &g_lastStatus);
        Serial.printf("BLE: BLECommand advertising name=%s\n", FW_DEVICE_NAME);
    }
    return ok;
}

bool sysBlePoll(const FwConfig *config, const FwSession *session)
{
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    BluetoothCommandAPI::loop();
    if (!g_started || !BluetoothCommandAPI::is_connected()) {
        return true;
    }
    return bleNotifyStatusChanges(session);
}
