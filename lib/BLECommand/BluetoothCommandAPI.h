/*
 * BluetoothCommandAPI - BLE command transport for Arduino ESP32.
 * Author: H1M4W4R1
 */

#ifndef BLUETOOTH_COMMAND_API_H
#define BLUETOOTH_COMMAND_API_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wundef"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#pragma GCC diagnostic pop

#include <stddef.h>
#include <stdint.h>

#ifndef BLUETOOTH_COMMAND_API_MAX_COMMANDS
#define BLUETOOTH_COMMAND_API_MAX_COMMANDS 16u
#endif

#ifndef BLUETOOTH_COMMAND_API_MAX_PACKET_LENGTH
#define BLUETOOTH_COMMAND_API_MAX_PACKET_LENGTH 256u
#endif

#ifndef BLUETOOTH_COMMAND_API_MAX_DATA_FIELDS
#define BLUETOOTH_COMMAND_API_MAX_DATA_FIELDS 16u
#endif

#ifndef BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH
#define BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH 512u
#endif

#ifndef BLUETOOTH_COMMAND_API_DEFAULT_TRANSMIT_RATE_HZ
#define BLUETOOTH_COMMAND_API_DEFAULT_TRANSMIT_RATE_HZ 20u
#endif

typedef void (*bluetooth_command_handler_t)(const char *const *data);

typedef enum
{
    bluetooth_command_result_ok = 0,
    bluetooth_command_result_invalid_argument,
    bluetooth_command_result_not_started,
    bluetooth_command_result_packet_too_long,
    bluetooth_command_result_invalid_packet,
    bluetooth_command_result_not_connected,
    bluetooth_command_result_full,
    bluetooth_command_result_transmit_buffer_full
} bluetooth_command_result_t;

typedef struct
{
    const char *name;
    bluetooth_command_handler_t handler;
} bluetooth_command_entry_t;

class BluetoothCommandAPI : public BLEServerCallbacks, public BLECharacteristicCallbacks
{
public:
    static bluetooth_command_result_t begin();
    static bluetooth_command_result_t begin(const char *device_name);
    static bluetooth_command_result_t begin(const char *device_name, uint8_t lovense_x, uint8_t lovense_y, uint8_t lovense_z);
    static bluetooth_command_result_t begin(
        const char *device_name,
        const char *service_uuid,
        const char *tx_uuid,
        const char *rx_uuid,
        const char *nx_uuid);
    static bluetooth_command_result_t register_command(const char *name, bluetooth_command_handler_t handler);
    static bluetooth_command_result_t send(const char *command, const char *const *data = NULL, size_t data_count = 0u);
    static bluetooth_command_result_t notify(const char *command, const char *const *data = NULL, size_t data_count = 0u);
    static void loop();
    static bluetooth_command_result_t set_transmit_rate_hz(uint16_t transmit_rate_hz);
    static bool is_connected();

private:
    BluetoothCommandAPI();
    static bluetooth_command_result_t queue_command(
        BLECharacteristic *characteristic,
        char *transmit_buffer,
        size_t *transmit_length,
        const char *command,
        const char *const *data,
        size_t data_count);
    static void transmit_buffer(BLECharacteristic *characteristic, char *buffer, size_t *buffer_length);
    static void consume_tx_data(const uint8_t *data, size_t data_length);
    static void dispatch_packet();
    static void restart_advertising();
    static void clear_transmit_buffers();
    void onConnect(BLEServer *server) override;
    void onDisconnect(BLEServer *server) override;
    void onWrite(BLECharacteristic *characteristic) override;
    static BluetoothCommandAPI _callback_instance;
    static BLEServer *_server;
    static BLECharacteristic *_tx_characteristic;
    static BLECharacteristic *_rx_characteristic;
    static BLECharacteristic *_nx_characteristic;
    static bluetooth_command_entry_t _commands[BLUETOOTH_COMMAND_API_MAX_COMMANDS];
    static char _packet_buffer[BLUETOOTH_COMMAND_API_MAX_PACKET_LENGTH];
    static char _rx_transmit_buffer[BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH];
    static char _nx_transmit_buffer[BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH];
    static size_t _command_count;
    static size_t _packet_length;
    static size_t _rx_transmit_length;
    static size_t _nx_transmit_length;
    static uint32_t _last_transmit_ms;
    static uint32_t _transmit_interval_ms;
    static bool _is_connected;
    static bool _is_discarding_packet;
};

#endif /* BLUETOOTH_COMMAND_API_H */
