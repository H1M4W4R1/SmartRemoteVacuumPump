# VPController

ESP32 pressure controller firmware for `OST-RSP`.

Author: H1M4W4R1

## What It Does

`OST-RSP` controls a pump and valve from BLE and a simple RF remote. It reads an analog pressure sensor, exposes live session data over BLE, can run in manual or automatic pressure-control modes, and can enable OTA programming on demand.

The firmware boots with BLE only. Wi-Fi AP and OTA are disabled until the user writes `1` to the `OTA` BLE configuration characteristic.

## Controls

Button A controls the pump:

- If the pump is off, button A starts the pump.
- In manual mode, button A also locks the valve when starting the pump.
- In automatic modes, button A does not change valve state.
- In `automatic_keep` and `adaptive_keep`, button A follows the virtual controller state, so it suspends the controller even when the physical pump is currently idle at target pressure.
- If the pump or virtual controller is running, button A stops it and marks the pump as temporarily disabled.
- Pressing button A again manually reactivates the pump and clears temporary disable.

Button B controls the valve:

- Button B toggles the valve lock.
- Button B is ignored when valve remote control is disabled.

## Modes

`manual` lets the user control pump and valve directly.

`automatic_single` stops pumping when target pressure is reached. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `automatic_single`.

`automatic_keep` stops pumping when target pressure is reached and restarts when pressure moves outside the configured deadzone. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `automatic_keep`.

`adaptive_keep` uses PWM pump drive with an adaptive PID loop to hold the configured target pressure. It continuously estimates chamber response while pumping and leak/drift while idle, then adjusts pump duty at runtime for chambers that vary between usages.

Automatic modes include direction protection:

- Negative target: shutdown if current pressure goes above `50 hPa`.
- Positive target: shutdown if current pressure goes below `-50 hPa`.

## Calibration

Set `Calibration Active` to `1` over BLE when the valve is open and the sensor is reading atmospheric pressure. The firmware turns pump and valve outputs off before sampling, computes the divider multiplier from the 2.5 V zero-pressure point, stores it in NVS, and clears `Calibration Active`.

If no calibration exists in NVS, the firmware attempts auto-calibration on boot.

## BLE

BLE device name: `OST-RSP`

Values are sent as text:

- Boolean values: `0` or `1`
- Time values: seconds integer, no unit suffix
- Pressure values: hPa integer, no unit suffix
- Mode values: `manual`, `automatic_keep`, `automatic_single`, or `adaptive_keep`

Each characteristic has a Characteristic User Description (`0x2901`) descriptor with the friendly name listed below. Each service also exposes a `Service Name` characteristic for debug tools.

### Session Service

Service UUID: `ae615001-0000-4000-8000-0d670255c8ef`

| Name | Access | UUID | Format |
| --- | --- | --- | --- |
| Service Name | R | `ae615001-00ff-4000-8000-0d670255c8ef` | `Session` |
| Pump Active | RWN | `ae615001-0001-4000-8000-0d670255c8ef` | bool integer |
| Valve Active | RWN | `ae615001-0002-4000-8000-0d670255c8ef` | bool integer |
| Current Session Time | RN | `ae615001-0003-4000-8000-0d670255c8ef` | seconds integer |
| Current Pumping Time | RN | `ae615001-0004-4000-8000-0d670255c8ef` | seconds integer |
| Last Session Time | RN | `ae615001-0005-4000-8000-0d670255c8ef` | seconds integer |
| Total Session Time | RN | `ae615001-0006-4000-8000-0d670255c8ef` | seconds integer |
| Minimum Pressure | RN | `ae615001-0007-4000-8000-0d670255c8ef` | hPa integer |
| Maximum Pressure | RN | `ae615001-0008-4000-8000-0d670255c8ef` | hPa integer |
| Current Pressure | RWN | `ae615001-0009-4000-8000-0d670255c8ef` | hPa integer |

### Configuration Service

Service UUID: `ae615002-0000-4000-8000-0d670255c8ef`

| Name | Access | UUID | Format |
| --- | --- | --- | --- |
| Service Name | R | `ae615002-00ff-4000-8000-0d670255c8ef` | `Configuration` |
| Maximum Session Time | RW | `ae615002-0001-4000-8000-0d670255c8ef` | seconds integer |
| Maximum Pumping Time | RW | `ae615002-0002-4000-8000-0d670255c8ef` | seconds integer |
| Mode | RW | `ae615002-0003-4000-8000-0d670255c8ef` | string |
| Pump Remote Disabled | RW | `ae615002-0004-4000-8000-0d670255c8ef` | bool integer |
| Valve Remote Disabled | RW | `ae615002-0005-4000-8000-0d670255c8ef` | bool integer |
| Target Pressure | RW | `ae615002-0006-4000-8000-0d670255c8ef` | hPa integer |
| Default On | RW | `ae615002-0007-4000-8000-0d670255c8ef` | bool integer, stored in NVS |
| Calibration Active | RWN | `ae615002-0008-4000-8000-0d670255c8ef` | bool integer |
| Pressure Deadzone | RWN | `ae615002-0009-4000-8000-0d670255c8ef` | hPa integer, stored in NVS |
| OTA | RWN | `ae615002-000a-4000-8000-0d670255c8ef` | bool integer, runtime only |

## OTA

Write `1` to the `OTA` characteristic to stop BLE, start the Wi-Fi AP named `OST-RSP`, and enable ArduinoOTA programming. The AP password is `VPC-7KQ9-M4W2`. OTA itself has no password.

The `OTA` value is not stored in NVS. Rebooting the device returns to BLE-only startup with OTA disabled.

## Hardware

| Signal | GPIO |
| --- | ---: |
| RF_DATA | 26 |
| OUT_0 / Pump | 25 |
| OUT_1 / Valve | 17 |
| ANALOG / Pressure | 34 |

Pressure sensor: `XGZP6847A100KPGPN`

| Sensor voltage | Pressure |
| ---: | ---: |
| 0.5 V | -100 kPa |
| 1.5 V | -50 kPa |
| 2.5 V | 0 kPa |
| 3.5 V | 50 kPa |
| 4.5 V | 100 kPa |

The analog path is filtered with a `750R/330nF` low-pass filter and divided for 3.3 V ADC logic.

## RF Setup

RF button codes are hardcoded in `include/operation/fw_config.h`.

Open the serial monitor, press remote buttons, and copy logged unknown codes into the button A/B arrays.

## Build

```powershell
pio run
```
