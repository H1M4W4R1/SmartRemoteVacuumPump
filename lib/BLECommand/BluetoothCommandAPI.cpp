/*
 * BluetoothCommandAPI - BLE command transport for Arduino ESP32.
 * Author: H1M4W4R1
 */

#include "BluetoothCommandAPI.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *const tx_description = "TX Command Input";
static const char *const rx_description = "RX Command Output";
static const char *const nx_description = "NX Command Events";
static const char *const service_name_description = "Service Name";
static const uint16_t service_name_characteristic_uuid = 0x00FFu;
static const uint16_t characteristic_user_description_uuid = 0x2901u;
static const char *const default_lovense_device_name = "BluetoothCommandAPI";

static void send_protocol_error(const char *error_type)
{
    const char *const data[] = {error_type};
    const bluetooth_command_result_t result = BluetoothCommandAPI::send("ERR", data, 1u);
    (void)result;
}

static bool strings_equal_ignore_case(const char *left, const char *right)
{
    if ((left == NULL) || (right == NULL))
    {
        return false;
    }
    while ((*left != '\0') && (*right != '\0'))
    {
        if (tolower(static_cast<unsigned char>(*left)) != tolower(static_cast<unsigned char>(*right)))
        {
            return false;
        }
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static void add_user_description(BLECharacteristic *characteristic, const char *description)
{
    BLEDescriptor *const descriptor = new BLEDescriptor(BLEUUID(characteristic_user_description_uuid));
    descriptor->setValue(description);
    characteristic->addDescriptor(descriptor);
}

static void add_client_configuration(BLECharacteristic *characteristic)
{
    BLE2902 *const descriptor = new BLE2902();
    characteristic->addDescriptor(descriptor);
}

static bool format_lovense_generation_three_uuid(
    char *buffer, size_t buffer_size, uint8_t lovense_x, uint8_t lovense_y, uint8_t lovense_z, uint8_t endpoint)
{
    const int result = snprintf(
        buffer, buffer_size, "%1X%1X30000%u-002%1X-4bd4-bbd5-a6920e4c5653",
        lovense_x, lovense_y, endpoint, lovense_z);
    return (result == 36) && (static_cast<size_t>(result) < buffer_size);
}

BluetoothCommandAPI BluetoothCommandAPI::_callback_instance;
BLEServer *BluetoothCommandAPI::_server = NULL;
BLECharacteristic *BluetoothCommandAPI::_tx_characteristic = NULL;
BLECharacteristic *BluetoothCommandAPI::_rx_characteristic = NULL;
BLECharacteristic *BluetoothCommandAPI::_nx_characteristic = NULL;
bluetooth_command_entry_t BluetoothCommandAPI::_commands[BLUETOOTH_COMMAND_API_MAX_COMMANDS] = {};
char BluetoothCommandAPI::_packet_buffer[BLUETOOTH_COMMAND_API_MAX_PACKET_LENGTH] = {};
char BluetoothCommandAPI::_rx_transmit_buffer[BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH] = {};
char BluetoothCommandAPI::_nx_transmit_buffer[BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH] = {};
size_t BluetoothCommandAPI::_command_count = 0u;
size_t BluetoothCommandAPI::_packet_length = 0u;
size_t BluetoothCommandAPI::_rx_transmit_length = 0u;
size_t BluetoothCommandAPI::_nx_transmit_length = 0u;
uint32_t BluetoothCommandAPI::_last_transmit_ms = 0u;
uint32_t BluetoothCommandAPI::_transmit_interval_ms = 1000u / BLUETOOTH_COMMAND_API_DEFAULT_TRANSMIT_RATE_HZ;
bool BluetoothCommandAPI::_is_connected = false;
bool BluetoothCommandAPI::_is_discarding_packet = false;

BluetoothCommandAPI::BluetoothCommandAPI()
{
}

bluetooth_command_result_t BluetoothCommandAPI::begin()
{
    return begin(default_lovense_device_name);
}

bluetooth_command_result_t BluetoothCommandAPI::begin(const char *device_name)
{
    return begin(device_name, 0x4u, 0x0u, 0x3u);
}

bluetooth_command_result_t BluetoothCommandAPI::begin(
    const char *device_name, uint8_t lovense_x, uint8_t lovense_y, uint8_t lovense_z)
{
    char service_uuid[37] = {};
    char tx_uuid[37] = {};
    char rx_uuid[37] = {};
    char nx_uuid[37] = {};
    if (((lovense_x != 0x4u) && (lovense_x != 0x5u)) || (lovense_y > 0x0Fu)
        || ((lovense_z != 0x3u) && (lovense_z != 0x4u)))
    {
        return bluetooth_command_result_invalid_argument;
    }
    if (!format_lovense_generation_three_uuid(service_uuid, sizeof(service_uuid), lovense_x, lovense_y, lovense_z, 1u)
        || !format_lovense_generation_three_uuid(tx_uuid, sizeof(tx_uuid), lovense_x, lovense_y, lovense_z, 2u)
        || !format_lovense_generation_three_uuid(rx_uuid, sizeof(rx_uuid), lovense_x, lovense_y, lovense_z, 3u)
        || !format_lovense_generation_three_uuid(nx_uuid, sizeof(nx_uuid), lovense_x, lovense_y, lovense_z, 4u))
    {
        return bluetooth_command_result_invalid_argument;
    }
    return begin(device_name, service_uuid, tx_uuid, rx_uuid, nx_uuid);
}

bluetooth_command_result_t BluetoothCommandAPI::begin(
    const char *device_name, const char *service_uuid, const char *tx_uuid, const char *rx_uuid, const char *nx_uuid)
{
    if ((device_name == NULL) || (service_uuid == NULL) || (tx_uuid == NULL) || (rx_uuid == NULL) || (nx_uuid == NULL))
    {
        return bluetooth_command_result_invalid_argument;
    }
    if (_server != NULL)
    {
        return bluetooth_command_result_not_started;
    }
    BLEDevice::init(device_name);
    _server = BLEDevice::createServer();
    if (_server == NULL)
    {
        return bluetooth_command_result_not_started;
    }
    _server->setCallbacks(&_callback_instance);
    BLEService *const service = _server->createService(service_uuid);
    if (service == NULL)
    {
        return bluetooth_command_result_not_started;
    }
    _tx_characteristic = service->createCharacteristic(tx_uuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    _rx_characteristic = service->createCharacteristic(rx_uuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    _nx_characteristic = service->createCharacteristic(nx_uuid, BLECharacteristic::PROPERTY_NOTIFY);
    if ((_tx_characteristic == NULL) || (_rx_characteristic == NULL) || (_nx_characteristic == NULL))
    {
        return bluetooth_command_result_not_started;
    }
    _tx_characteristic->setCallbacks(&_callback_instance);
    add_user_description(_tx_characteristic, tx_description);
    add_user_description(_rx_characteristic, rx_description);
    add_user_description(_nx_characteristic, nx_description);
    add_client_configuration(_rx_characteristic);
    add_client_configuration(_nx_characteristic);
    BLECharacteristic *const service_name = service->createCharacteristic(
        BLEUUID(service_name_characteristic_uuid), BLECharacteristic::PROPERTY_READ);
    if (service_name == NULL)
    {
        return bluetooth_command_result_not_started;
    }
    service_name->setValue(device_name);
    add_user_description(service_name, service_name_description);
    clear_transmit_buffers();
    _last_transmit_ms = millis();
    service->start();
    restart_advertising();
    return bluetooth_command_result_ok;
}

bluetooth_command_result_t BluetoothCommandAPI::register_command(const char *name, bluetooth_command_handler_t handler)
{
    if ((name == NULL) || (*name == '\0') || (handler == NULL))
    {
        return bluetooth_command_result_invalid_argument;
    }
    if (_command_count >= BLUETOOTH_COMMAND_API_MAX_COMMANDS)
    {
        return bluetooth_command_result_full;
    }
    _commands[_command_count].name = name;
    _commands[_command_count].handler = handler;
    ++_command_count;
    return bluetooth_command_result_ok;
}

bluetooth_command_result_t BluetoothCommandAPI::send(const char *command, const char *const *data, size_t data_count)
{
    return queue_command(_rx_characteristic, _rx_transmit_buffer, &_rx_transmit_length, command, data, data_count);
}

bluetooth_command_result_t BluetoothCommandAPI::notify(const char *command, const char *const *data, size_t data_count)
{
    return queue_command(_nx_characteristic, _nx_transmit_buffer, &_nx_transmit_length, command, data, data_count);
}

void BluetoothCommandAPI::loop()
{
    const uint32_t current_time_ms = millis();
    if (!_is_connected || ((current_time_ms - _last_transmit_ms) < _transmit_interval_ms))
    {
        return;
    }
    transmit_buffer(_rx_characteristic, _rx_transmit_buffer, &_rx_transmit_length);
    transmit_buffer(_nx_characteristic, _nx_transmit_buffer, &_nx_transmit_length);
    _last_transmit_ms = current_time_ms;
}

bluetooth_command_result_t BluetoothCommandAPI::set_transmit_rate_hz(uint16_t transmit_rate_hz)
{
    if ((transmit_rate_hz == 0u) || (transmit_rate_hz > 1000u))
    {
        return bluetooth_command_result_invalid_argument;
    }
    _transmit_interval_ms = 1000u / transmit_rate_hz;
    return bluetooth_command_result_ok;
}

bool BluetoothCommandAPI::is_connected()
{
    return _is_connected;
}

bluetooth_command_result_t BluetoothCommandAPI::queue_command(
    BLECharacteristic *characteristic, char *transmit_buffer, size_t *transmit_length,
    const char *command, const char *const *data, size_t data_count)
{
    char packet[BLUETOOTH_COMMAND_API_MAX_PACKET_LENGTH] = {};
    int write_result = 0;
    size_t packet_length = 0u;
    if ((characteristic == NULL) || (transmit_buffer == NULL) || (transmit_length == NULL) || (command == NULL)
        || (*command == '\0') || ((data_count > 0u) && (data == NULL)))
    {
        return bluetooth_command_result_invalid_argument;
    }
    if (!_is_connected)
    {
        return bluetooth_command_result_not_connected;
    }
    write_result = snprintf(packet, sizeof(packet), "%s", command);
    if ((write_result < 0) || (static_cast<size_t>(write_result) >= sizeof(packet)))
    {
        return bluetooth_command_result_packet_too_long;
    }
    packet_length = static_cast<size_t>(write_result);
    for (size_t index = 0u; index < data_count; ++index)
    {
        if (data[index] == NULL)
        {
            return bluetooth_command_result_invalid_argument;
        }
        write_result = snprintf(packet + packet_length, sizeof(packet) - packet_length, ":%s", data[index]);
        if ((write_result < 0) || (static_cast<size_t>(write_result) >= (sizeof(packet) - packet_length)))
        {
            return bluetooth_command_result_packet_too_long;
        }
        packet_length += static_cast<size_t>(write_result);
    }
    if ((packet_length + 1u) >= sizeof(packet))
    {
        return bluetooth_command_result_packet_too_long;
    }
    if ((packet_length + *transmit_length + 1u) > BLUETOOTH_COMMAND_API_MAX_TRANSMIT_BUFFER_LENGTH)
    {
        return bluetooth_command_result_transmit_buffer_full;
    }
    packet[packet_length] = ';';
    memcpy(transmit_buffer + *transmit_length, packet, packet_length + 1u);
    *transmit_length += packet_length + 1u;
    return bluetooth_command_result_ok;
}

void BluetoothCommandAPI::transmit_buffer(BLECharacteristic *characteristic, char *buffer, size_t *buffer_length)
{
    if ((characteristic == NULL) || (buffer == NULL) || (buffer_length == NULL) || (*buffer_length == 0u))
    {
        return;
    }
    characteristic->setValue(reinterpret_cast<uint8_t *>(buffer), *buffer_length);
    characteristic->notify();
    *buffer_length = 0u;
}

void BluetoothCommandAPI::consume_tx_data(const uint8_t *data, size_t data_length)
{
    if (data == NULL)
    {
        return;
    }
    for (size_t index = 0u; index < data_length; ++index)
    {
        const char character = static_cast<char>(data[index]);
        if (character == ';')
        {
            if (!_is_discarding_packet)
            {
                _packet_buffer[_packet_length] = '\0';
                dispatch_packet();
            }
            else
            {
                send_protocol_error("packet_too_long");
            }
            _packet_length = 0u;
            _is_discarding_packet = false;
        }
        else if (!_is_discarding_packet && (_packet_length < (sizeof(_packet_buffer) - 1u)))
        {
            _packet_buffer[_packet_length] = character;
            ++_packet_length;
        }
        else
        {
            _packet_length = 0u;
            _is_discarding_packet = true;
        }
    }
}

void BluetoothCommandAPI::dispatch_packet()
{
    char *data[BLUETOOTH_COMMAND_API_MAX_DATA_FIELDS + 1u] = {};
    size_t data_count = 0u;
    char *command = _packet_buffer;
    if (_packet_length == 0u)
    {
        return;
    }
    for (char *character = _packet_buffer; *character != '\0'; ++character)
    {
        if (*character == ':')
        {
            *character = '\0';
            if (data_count >= BLUETOOTH_COMMAND_API_MAX_DATA_FIELDS)
            {
                send_protocol_error("invalid_arguments");
                return;
            }
            data[data_count] = character + 1;
            ++data_count;
        }
    }
    if (*command == '\0')
    {
        return;
    }
    data[data_count] = NULL;
    for (size_t index = 0u; index < _command_count; ++index)
    {
        if (strings_equal_ignore_case(command, _commands[index].name))
        {
            _commands[index].handler(data);
            return;
        }
    }
    send_protocol_error("unknown_command");
}

void BluetoothCommandAPI::restart_advertising()
{
    BLEAdvertising *const advertising = BLEDevice::getAdvertising();
    if (advertising != NULL)
    {
        advertising->start();
    }
}

void BluetoothCommandAPI::clear_transmit_buffers()
{
    _rx_transmit_length = 0u;
    _nx_transmit_length = 0u;
}

void BluetoothCommandAPI::onConnect(BLEServer *server)
{
    (void)server;
    _is_connected = true;
}

void BluetoothCommandAPI::onDisconnect(BLEServer *server)
{
    (void)server;
    _is_connected = false;
    clear_transmit_buffers();
    restart_advertising();
}

void BluetoothCommandAPI::onWrite(BLECharacteristic *characteristic)
{
    if (characteristic == NULL)
    {
        return;
    }
    const String value = characteristic->getValue();
    consume_tx_data(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}
