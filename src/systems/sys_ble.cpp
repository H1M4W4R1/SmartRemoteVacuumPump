#include <systems/sys_ble.h>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEDescriptor.h>
#include <BLEServer.h>
#include <assert.h>

#include <operation/fw_config.h>
#include <operation/fw_controller.h>

#define BLE_UUID_SESSION_SERVICE "ae615001-0000-4000-8000-0d670255c8ef"
#define BLE_UUID_CONFIG_SERVICE "ae615002-0000-4000-8000-0d670255c8ef"
#define BLE_UUID_SESSION_SERVICE_NAME "ae615001-00ff-4000-8000-0d670255c8ef"
#define BLE_UUID_CONFIG_SERVICE_NAME "ae615002-00ff-4000-8000-0d670255c8ef"
#define BLE_UUID_SESSION_PUMP "ae615001-0001-4000-8000-0d670255c8ef"
#define BLE_UUID_SESSION_VALVE "ae615001-0002-4000-8000-0d670255c8ef"
#define BLE_UUID_SESSION_TIME "ae615001-0003-4000-8000-0d670255c8ef"
#define BLE_UUID_PUMP_TIME "ae615001-0004-4000-8000-0d670255c8ef"
#define BLE_UUID_LAST_TIME "ae615001-0005-4000-8000-0d670255c8ef"
#define BLE_UUID_TOTAL_TIME "ae615001-0006-4000-8000-0d670255c8ef"
#define BLE_UUID_MIN_PRESSURE "ae615001-0007-4000-8000-0d670255c8ef"
#define BLE_UUID_MAX_PRESSURE "ae615001-0008-4000-8000-0d670255c8ef"
#define BLE_UUID_CURRENT_PRESSURE "ae615001-0009-4000-8000-0d670255c8ef"
#define BLE_UUID_MAX_SESSION "ae615002-0001-4000-8000-0d670255c8ef"
#define BLE_UUID_MAX_PUMPING "ae615002-0002-4000-8000-0d670255c8ef"
#define BLE_UUID_MODE "ae615002-0003-4000-8000-0d670255c8ef"
#define BLE_UUID_PUMP_LOCK "ae615002-0004-4000-8000-0d670255c8ef"
#define BLE_UUID_VALVE_LOCK "ae615002-0005-4000-8000-0d670255c8ef"
#define BLE_UUID_TARGET "ae615002-0006-4000-8000-0d670255c8ef"
#define BLE_UUID_DEFAULT_ON "ae615002-0007-4000-8000-0d670255c8ef"
#define BLE_UUID_CALIBRATION "ae615002-0008-4000-8000-0d670255c8ef"
#define BLE_UUID_DEADZONE "ae615002-0009-4000-8000-0d670255c8ef"

#define BLE_SESSION_HANDLE_COUNT 48U
#define BLE_CONFIG_HANDLE_COUNT 36U

enum BleWriteId {
    BLE_WRITE_PUMP = 1,
    BLE_WRITE_VALVE = 2,
    BLE_WRITE_MAX_SESSION = 3,
    BLE_WRITE_MAX_PUMPING = 4,
    BLE_WRITE_MODE = 5,
    BLE_WRITE_PUMP_LOCK = 6,
    BLE_WRITE_VALVE_LOCK = 7,
    BLE_WRITE_TARGET = 8,
    BLE_WRITE_DEFAULT_ON = 9,
    BLE_WRITE_CALIBRATION = 10,
    BLE_WRITE_DEADZONE = 11,
    BLE_WRITE_CURRENT_PRESSURE = 12
};

static FwConfig *g_config = nullptr;
static FwSession *g_session = nullptr;
static BLEServer *g_server = nullptr;
static BLECharacteristic *g_pump = nullptr;
static BLECharacteristic *g_valve = nullptr;
static BLECharacteristic *g_sessionTime = nullptr;
static BLECharacteristic *g_pumpTime = nullptr;
static BLECharacteristic *g_lastTime = nullptr;
static BLECharacteristic *g_totalTime = nullptr;
static BLECharacteristic *g_currentPressure = nullptr;
static BLECharacteristic *g_minPressure = nullptr;
static BLECharacteristic *g_maxPressure = nullptr;
static BLECharacteristic *g_maxSession = nullptr;
static BLECharacteristic *g_maxPumping = nullptr;
static BLECharacteristic *g_mode = nullptr;
static BLECharacteristic *g_pumpLock = nullptr;
static BLECharacteristic *g_valveLock = nullptr;
static BLECharacteristic *g_target = nullptr;
static BLECharacteristic *g_defaultOn = nullptr;
static BLECharacteristic *g_calibration = nullptr;
static BLECharacteristic *g_deadzone = nullptr;
static bool g_connected = false;
static uint32_t g_lastNotifyMs = 0UL;

static String bleReadText(BLECharacteristic *characteristic)
{
    assert(characteristic != nullptr);
    assert(g_config != nullptr);
    if (characteristic == nullptr) {
        return "";
    }
    return characteristic->getValue();
}

static bool bleParseBool(const String &text, bool *value)
{
    assert(value != nullptr);
    assert(text.length() < 16U);
    if (value == nullptr) {
        return false;
    }
    if (text == "0") {
        *value = false;
        return true;
    }
    if (text == "1") {
        *value = true;
        return true;
    }
    return false;
}

static bool bleParseInt32(const String &text, int32_t *value)
{
    char *endPtr = nullptr;
    long parsed = 0L;
    assert(value != nullptr);
    assert(text.length() < 24U);
    if ((value == nullptr) || (text.length() >= 24U)) {
        return false;
    }
    parsed = strtol(text.c_str(), &endPtr, 10);
    if ((endPtr == nullptr) || (*endPtr != '\0')) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static bool bleSet(BLECharacteristic *characteristic, const String &value, bool notify)
{
    assert(characteristic != nullptr);
    assert(value.length() < 64U);
    if (characteristic == nullptr) {
        return false;
    }
    characteristic->setValue(value.c_str());
    if (notify) {
        characteristic->notify();
    }
    return true;
}

class BleServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override
    {
        assert(server != nullptr);
        assert(g_server != nullptr);
        g_connected = true;
        Serial.println("BLE: connected");
    }

    void onDisconnect(BLEServer *server) override
    {
        assert(server != nullptr);
        assert(g_server != nullptr);
        g_connected = false;
        Serial.println("BLE: disconnected, advertising restarted");
        server->startAdvertising();
    }
};

class BleWriteCallbacks final : public BLECharacteristicCallbacks {
public:
    explicit BleWriteCallbacks(BleWriteId id) : m_id(id) {}

    void onWrite(BLECharacteristic *characteristic) override
    {
        bool ok = false;
        assert(characteristic != nullptr);
        assert(g_config != nullptr);
        if ((characteristic == nullptr) || (g_config == nullptr) || (g_session == nullptr)) {
            return;
        }
        ok = handleWrite(characteristic);
        Serial.printf("BLE: write id=%d ok=%u value=%s\n",
            (int)m_id, ok ? 1U : 0U, bleReadText(characteristic).c_str());
    }

private:
    BleWriteId m_id;

    bool handleWrite(BLECharacteristic *characteristic)
    {
        String text = bleReadText(characteristic);
        bool boolValue = false;
        int32_t intValue = 0L;
        FwMode mode = FW_MODE_MANUAL;
        assert(characteristic != nullptr);
        assert(text.length() < 64U);
        if (m_id == BLE_WRITE_MODE) {
            return fwModeFromText(text, &mode) && writeMode(mode);
        }
        if (!bleParseInt32(text, &intValue)) {
            return false;
        }
        if ((m_id == BLE_WRITE_PUMP) || (m_id == BLE_WRITE_VALVE) || (m_id == BLE_WRITE_PUMP_LOCK)
            || (m_id == BLE_WRITE_VALVE_LOCK) || (m_id == BLE_WRITE_DEFAULT_ON)
            || (m_id == BLE_WRITE_CALIBRATION)) {
            if (!bleParseBool(text, &boolValue)) {
                return false;
            }
        }
        return writeInteger(intValue, boolValue);
    }

    bool writeMode(FwMode mode)
    {
        assert(g_config != nullptr);
        assert(mode <= FW_MODE_AUTOMATIC_SINGLE);
        g_config->mode = mode;
        return true;
    }

    bool writeInteger(int32_t intValue, bool boolValue)
    {
        assert(g_config != nullptr);
        assert(g_session != nullptr);
        if (m_id == BLE_WRITE_PUMP) {
            return fwControllerSetPump(g_session, boolValue, true);
        }
        if (m_id == BLE_WRITE_VALVE) {
            return fwControllerSetValve(g_session, boolValue);
        }
        if (m_id == BLE_WRITE_CURRENT_PRESSURE) {
            g_session->currentPressureHpa = intValue;
            return true;
        }
        if (m_id == BLE_WRITE_MAX_SESSION) {
            g_config->maxSessionSeconds = (uint32_t)intValue;
        }
        if (m_id == BLE_WRITE_MAX_PUMPING) {
            g_config->maxPumpingSeconds = (uint32_t)intValue;
        }
        return writeConfigInteger(intValue, boolValue);
    }

    bool writeConfigInteger(int32_t intValue, bool boolValue)
    {
        assert(g_config != nullptr);
        assert(g_session != nullptr);
        if (m_id == BLE_WRITE_PUMP_LOCK) {
            g_config->pumpRemoteDisabled = boolValue;
        }
        if (m_id == BLE_WRITE_VALVE_LOCK) {
            g_config->valveRemoteDisabled = boolValue;
        }
        if (m_id == BLE_WRITE_TARGET) {
            g_config->targetPressureHpa = intValue;
        }
        if (m_id == BLE_WRITE_DEFAULT_ON) {
            g_config->defaultOn = boolValue;
            return fwConfigRequestSave(g_config);
        }
        if (m_id == BLE_WRITE_CALIBRATION) {
            g_config->calibrationActive = boolValue;
        }
        if (m_id == BLE_WRITE_DEADZONE) {
            if ((intValue < FW_MIN_PRESSURE_DEADZONE_HPA) || (intValue > FW_MAX_PRESSURE_DEADZONE_HPA)) {
                return false;
            }
            g_config->pressureDeadzoneHpa = intValue;
            return fwConfigRequestSave(g_config);
        }
        return true;
    }
};

static bool bleAddCud(BLECharacteristic *characteristic, const char *name)
{
    BLEDescriptor *description = nullptr;
    assert(characteristic != nullptr);
    assert(name != nullptr);
    if ((characteristic == nullptr) || (name == nullptr)) {
        return false;
    }
    description = new BLEDescriptor(BLEUUID((uint16_t)0x2901), 48U);
    if (description == nullptr) {
        return false;
    }
    description->setAccessPermissions(ESP_GATT_PERM_READ);
    description->setValue(String(name));
    characteristic->addDescriptor(description);
    return true;
}

static BLECharacteristic *bleCreate(BLEService *service, const char *uuid, uint32_t props, const char *name)
{
    BLECharacteristic *characteristic = nullptr;
    bool hasClientConfig = false;
    assert(service != nullptr);
    assert(uuid != nullptr);
    assert(name != nullptr);
    if ((service == nullptr) || (uuid == nullptr) || (name == nullptr)) {
        return nullptr;
    }
    characteristic = service->createCharacteristic(uuid, props);
    if (characteristic != nullptr) {
        if (!bleAddCud(characteristic, name)) {
            return nullptr;
        }
        hasClientConfig = ((props & BLECharacteristic::PROPERTY_NOTIFY) != 0U)
            || ((props & BLECharacteristic::PROPERTY_INDICATE) != 0U);
        if (hasClientConfig) {
            characteristic->addDescriptor(new BLE2902());
        }
    }
    return characteristic;
}

static bool bleCreateServiceName(BLEService *service, const char *uuid, const char *name)
{
    BLECharacteristic *characteristic = nullptr;
    assert(service != nullptr);
    assert(uuid != nullptr);
    if ((service == nullptr) || (uuid == nullptr) || (name == nullptr)) {
        return false;
    }
    characteristic = bleCreate(service, uuid, BLECharacteristic::PROPERTY_READ, "Service Name");
    if (characteristic == nullptr) {
        return false;
    }
    characteristic->setValue(name);
    return true;
}

static bool bleCreateSession(BLEService *service)
{
    uint32_t rn = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY;
    uint32_t rwn = rn | BLECharacteristic::PROPERTY_WRITE;
    assert(service != nullptr);
    assert(g_server != nullptr);
    if (service == nullptr) {
        return false;
    }
    if (!bleCreateServiceName(service, BLE_UUID_SESSION_SERVICE_NAME, "Session")) {
        return false;
    }
    g_pump = bleCreate(service, BLE_UUID_SESSION_PUMP, rwn, "Pump Active");
    g_valve = bleCreate(service, BLE_UUID_SESSION_VALVE, rwn, "Valve Active");
    g_sessionTime = bleCreate(service, BLE_UUID_SESSION_TIME, rn, "Current Session Time");
    g_pumpTime = bleCreate(service, BLE_UUID_PUMP_TIME, rn, "Current Pumping Time");
    g_lastTime = bleCreate(service, BLE_UUID_LAST_TIME, rn, "Last Session Time");
    g_totalTime = bleCreate(service, BLE_UUID_TOTAL_TIME, rn, "Total Session Time");
    g_currentPressure = bleCreate(service, BLE_UUID_CURRENT_PRESSURE, rwn, "Current Pressure");
    g_minPressure = bleCreate(service, BLE_UUID_MIN_PRESSURE, rn, "Minimum Pressure");
    g_maxPressure = bleCreate(service, BLE_UUID_MAX_PRESSURE, rn, "Maximum Pressure");
    if ((g_pump == nullptr) || (g_valve == nullptr) || (g_currentPressure == nullptr)
        || (g_maxPressure == nullptr)) {
        return false;
    }
    g_pump->setCallbacks(new BleWriteCallbacks(BLE_WRITE_PUMP));
    g_valve->setCallbacks(new BleWriteCallbacks(BLE_WRITE_VALVE));
    g_currentPressure->setCallbacks(new BleWriteCallbacks(BLE_WRITE_CURRENT_PRESSURE));
    return g_maxPressure != nullptr;
}

static bool bleCreateConfig(BLEService *service)
{
    uint32_t rw = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
    uint32_t rwn = rw | BLECharacteristic::PROPERTY_NOTIFY;
    assert(service != nullptr);
    assert(g_config != nullptr);
    if (service == nullptr) {
        return false;
    }
    if (!bleCreateServiceName(service, BLE_UUID_CONFIG_SERVICE_NAME, "Configuration")) {
        return false;
    }
    g_maxSession = bleCreate(service, BLE_UUID_MAX_SESSION, rw, "Maximum Session Time");
    g_maxPumping = bleCreate(service, BLE_UUID_MAX_PUMPING, rw, "Maximum Pumping Time");
    g_mode = bleCreate(service, BLE_UUID_MODE, rw, "Mode");
    g_pumpLock = bleCreate(service, BLE_UUID_PUMP_LOCK, rw, "Pump Remote Disabled");
    g_valveLock = bleCreate(service, BLE_UUID_VALVE_LOCK, rw, "Valve Remote Disabled");
    g_target = bleCreate(service, BLE_UUID_TARGET, rw, "Target Pressure");
    g_defaultOn = bleCreate(service, BLE_UUID_DEFAULT_ON, rw, "Default On");
    g_calibration = bleCreate(service, BLE_UUID_CALIBRATION, rwn, "Calibration Active");
    g_deadzone = bleCreate(service, BLE_UUID_DEADZONE, rwn, "Pressure Deadzone");
    return g_deadzone != nullptr;
}

static bool bleAssignCallbacks(void)
{
    assert(g_maxSession != nullptr);
    assert(g_deadzone != nullptr);
    if ((g_maxSession == nullptr) || (g_deadzone == nullptr)) {
        return false;
    }
    g_maxSession->setCallbacks(new BleWriteCallbacks(BLE_WRITE_MAX_SESSION));
    g_maxPumping->setCallbacks(new BleWriteCallbacks(BLE_WRITE_MAX_PUMPING));
    g_mode->setCallbacks(new BleWriteCallbacks(BLE_WRITE_MODE));
    g_pumpLock->setCallbacks(new BleWriteCallbacks(BLE_WRITE_PUMP_LOCK));
    g_valveLock->setCallbacks(new BleWriteCallbacks(BLE_WRITE_VALVE_LOCK));
    g_target->setCallbacks(new BleWriteCallbacks(BLE_WRITE_TARGET));
    g_defaultOn->setCallbacks(new BleWriteCallbacks(BLE_WRITE_DEFAULT_ON));
    g_calibration->setCallbacks(new BleWriteCallbacks(BLE_WRITE_CALIBRATION));
    g_deadzone->setCallbacks(new BleWriteCallbacks(BLE_WRITE_DEADZONE));
    return true;
}

static bool bleUpdateValues(const FwConfig *config, const FwSession *session, bool notify)
{
    bool ok = true;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    ok = bleSet(g_pump, String(session->pumpActive ? 1 : 0), notify) && ok;
    ok = bleSet(g_valve, String(session->valveActive ? 1 : 0), notify) && ok;
    ok = bleSet(g_sessionTime, String(session->currentSessionSeconds), notify) && ok;
    ok = bleSet(g_pumpTime, String(session->currentPumpingSeconds), notify) && ok;
    ok = bleSet(g_lastTime, String(session->lastSessionSeconds), notify) && ok;
    ok = bleSet(g_totalTime, String(session->totalSessionSeconds), notify) && ok;
    ok = bleSet(g_currentPressure, String(session->currentPressureHpa), notify) && ok;
    ok = bleSet(g_minPressure, String(session->minPressureHpa), notify) && ok;
    ok = bleSet(g_maxPressure, String(session->maxPressureHpa), notify) && ok;
    return ok;
}

static bool bleUpdateConfigValues(const FwConfig *config, bool notify)
{
    bool ok = true;
    bool notifyActive = false;
    assert(config != nullptr);
    assert(g_deadzone != nullptr);
    if (config == nullptr) {
        return false;
    }
    notifyActive = notify && g_connected;
    ok = bleSet(g_maxSession, String(config->maxSessionSeconds), false) && ok;
    ok = bleSet(g_maxPumping, String(config->maxPumpingSeconds), false) && ok;
    ok = bleSet(g_mode, String(fwModeToText(config->mode)), false) && ok;
    ok = bleSet(g_pumpLock, String(config->pumpRemoteDisabled ? 1 : 0), false) && ok;
    ok = bleSet(g_valveLock, String(config->valveRemoteDisabled ? 1 : 0), false) && ok;
    ok = bleSet(g_target, String(config->targetPressureHpa), false) && ok;
    ok = bleSet(g_defaultOn, String(config->defaultOn ? 1 : 0), false) && ok;
    ok = bleSet(g_calibration, String(config->calibrationActive ? 1 : 0), notifyActive) && ok;
    ok = bleSet(g_deadzone, String(config->pressureDeadzoneHpa), notifyActive) && ok;
    return ok;
}

bool sysBleInit(FwConfig *config, FwSession *session)
{
    BLEService *sessionService = nullptr;
    BLEService *configService = nullptr;
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    g_config = config;
    g_session = session;
    BLEDevice::init(FW_DEVICE_NAME);
    g_server = BLEDevice::createServer();
    g_server->setCallbacks(new BleServerCallbacks());
    sessionService = g_server->createService(BLEUUID(BLE_UUID_SESSION_SERVICE), BLE_SESSION_HANDLE_COUNT);
    configService = g_server->createService(BLEUUID(BLE_UUID_CONFIG_SERVICE), BLE_CONFIG_HANDLE_COUNT);
    ok = bleCreateSession(sessionService);
    ok = bleCreateConfig(configService) && ok;
    ok = bleAssignCallbacks() && ok;
    ok = bleUpdateValues(config, session, false) && ok;
    ok = bleUpdateConfigValues(config, false) && ok;
    sessionService->start();
    configService->start();
    g_server->getAdvertising()->addServiceUUID(BLE_UUID_SESSION_SERVICE);
    g_server->getAdvertising()->addServiceUUID(BLE_UUID_CONFIG_SERVICE);
    g_server->getAdvertising()->start();
    Serial.printf("BLE: advertising name=%s\n", FW_DEVICE_NAME);
    return ok;
}

bool sysBlePoll(const FwConfig *config, const FwSession *session)
{
    uint32_t nowMs = millis();
    bool ok = false;
    assert(config != nullptr);
    assert(session != nullptr);
    if ((config == nullptr) || (session == nullptr)) {
        return false;
    }
    if ((nowMs - g_lastNotifyMs) < FW_BLE_UPDATE_PERIOD_MS) {
        return true;
    }
    g_lastNotifyMs = nowMs;
    ok = bleUpdateValues(config, session, g_connected);
    ok = bleUpdateConfigValues(config, g_connected) && ok;
    return ok;
}
