#include <systems/sys_rf.h>

#include <RCSwitch.h>
#include <assert.h>

#include <operation/fw_config.h>

static RCSwitch g_rf;
static unsigned long g_lastCode = 0UL;
static uint32_t g_lastCodeMs = 0UL;

static bool sysRfCodeInList(unsigned long code, const unsigned long *codes, size_t count)
{
    size_t index = 0U;
    assert(codes != nullptr);
    assert(count <= 8U);
    if (codes == nullptr) {
        return false;
    }
    for (index = 0U; index < count; index++) {
        if (codes[index] == code) {
            return true;
        }
    }
    return false;
}

static SysRfButton sysRfClassify(unsigned long code)
{
    bool matched = false;
    assert(FW_RF_BUTTON_A_CODES_COUNT <= 8);
    assert(FW_RF_BUTTON_B_CODES_COUNT <= 8);
    matched = sysRfCodeInList(code, FW_RF_BUTTON_A_CODES, FW_RF_BUTTON_A_CODES_COUNT);
    if (matched) {
        return SYS_RF_BUTTON_A;
    }
    matched = sysRfCodeInList(code, FW_RF_BUTTON_B_CODES, FW_RF_BUTTON_B_CODES_COUNT);
    if (matched) {
        return SYS_RF_BUTTON_B;
    }
    return SYS_RF_BUTTON_NONE;
}

bool sysRfInit(void)
{
    assert(FW_PIN_RF_DATA >= 0);
    assert(FW_RF_DEBOUNCE_MS > 0UL);
    g_rf.enableReceive(digitalPinToInterrupt(FW_PIN_RF_DATA));
    Serial.printf("RF: receiver enabled on GPIO %d\n", FW_PIN_RF_DATA);
    return true;
}

bool sysRfPoll(SysRfButton *button)
{
    unsigned long code = 0UL;
    uint32_t nowMs = 0UL;
    SysRfButton detected = SYS_RF_BUTTON_NONE;
    assert(button != nullptr);
    assert(FW_RF_BUTTON_A_CODES_COUNT > 0);
    if (button == nullptr) {
        return false;
    }
    *button = SYS_RF_BUTTON_NONE;
    if (!g_rf.available()) {
        return true;
    }
    code = g_rf.getReceivedValue();
    nowMs = millis();
    Serial.printf("RF: value=%lu bits=%u protocol=%u delay=%u\n",
        code, g_rf.getReceivedBitlength(), g_rf.getReceivedProtocol(), g_rf.getReceivedDelay());
    g_rf.resetAvailable();
    if ((code == g_lastCode) && ((nowMs - g_lastCodeMs) < FW_RF_DEBOUNCE_MS)) {
        return true;
    }
    g_lastCode = code;
    g_lastCodeMs = nowMs;
    detected = sysRfClassify(code);
    if (detected == SYS_RF_BUTTON_NONE) {
        Serial.printf("RF: unknown code %lu, add it to fw_config.h if needed\n", code);
    }
    *button = detected;
    return true;
}
