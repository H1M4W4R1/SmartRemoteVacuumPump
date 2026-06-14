<div align="center">
  <h1>Smart Remote Vacuum Pump</h1>
  <img src="https://github.com/H1M4W4R1/RemoteVacuumPump/blob/master/images/pump.webp?raw=true" alt="Device photo"/>
</div>

This project is a direct upgrade of previous one available <a href="https://github.com/H1M4W4R1/RemoteVacuumPump">here</a>.
Firmware controls a normally-open valve and three parallel 555-type pumps using cheap -100 to 100kPa pressure sensor as feedback.
Pumps can be controlled using 433MHz ASK remote or using BLE.

Current version: `1.0.2`

## Controls

Button A controls the pump:

- If the pump is off, button A starts the pump.
- In manual mode, button A also locks the valve when starting the pump.
- In automatic modes, button A does not change valve state.
- In `target_hysteresis` and `target_adaptive`, button A follows the virtual controller state, so it suspends the controller even when the physical pump is currently idle at target pressure.
- If the pump or virtual controller is running, button A stops it and marks the pump as temporarily disabled.
- Pressing button A again manually reactivates the pump and clears temporary disable.

Button B controls the valve:

- Button B toggles the valve lock.
- Button B is ignored when valve remote control is disabled.

## Modes

`manual` lets the user control pump and valve directly.

`target_oneshot` stops pumping when target pressure is reached. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `target_oneshot`.

`target_hysteresis` stops pumping when target pressure is reached and restarts when pressure moves outside the configured deadzone. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `target_hysteresis`.

`target_adaptive` uses PWM pump drive with an adaptive PID loop to hold the configured target pressure. It continuously estimates chamber response while pumping and leak/drift while idle, then adjusts pump duty at runtime for chambers that vary between usages.

## Calibration

Set `Calibration Active` to `1` over BLE when the valve is open and the sensor is reading atmospheric pressure. The firmware turns pump and valve outputs off before sampling, computes the divider multiplier from the 2.5 V zero-pressure point, stores it in NVS, and clears `Calibration Active`.

If no calibration exists in NVS, the firmware attempts auto-calibration on boot.

## BLE

BLE device name: `OST-RSP`

Values are sent as text:

- Boolean values: `0` or `1`
- Time values: seconds integer, no unit suffix
- Pressure values: hPa integer, no unit suffix
- Mode values: `manual`, `target_hysteresis`, `target_oneshot`, or `target_adaptive`

Each characteristic has a Characteristic User Description (`0x2901`) descriptor with the friendly name listed below. Each service also exposes a `Service Name` characteristic for debug tools.

### Session Service

Service UUID: `ae615001-0000-4000-8000-0d670255c8ef`

| Name                 | Access | UUID                                   | Format          |
|----------------------|--------|----------------------------------------|-----------------|
| Service Name         | R      | `ae615001-00ff-4000-8000-0d670255c8ef` | `Session`       |
| Pump Active          | RWN    | `ae615001-0001-4000-8000-0d670255c8ef` | bool integer    |
| Valve Active         | RWN    | `ae615001-0002-4000-8000-0d670255c8ef` | bool integer    |
| Current Session Time | RN     | `ae615001-0003-4000-8000-0d670255c8ef` | seconds integer |
| Current Pumping Time | RN     | `ae615001-0004-4000-8000-0d670255c8ef` | seconds integer |
| Last Session Time    | RN     | `ae615001-0005-4000-8000-0d670255c8ef` | seconds integer |
| Total Session Time   | RN     | `ae615001-0006-4000-8000-0d670255c8ef` | seconds integer |
| Minimum Pressure     | RN     | `ae615001-0007-4000-8000-0d670255c8ef` | hPa integer     |
| Maximum Pressure     | RN     | `ae615001-0008-4000-8000-0d670255c8ef` | hPa integer     |
| Current Pressure     | RWN    | `ae615001-0009-4000-8000-0d670255c8ef` | hPa integer     |

### Configuration Service

Service UUID: `ae615002-0000-4000-8000-0d670255c8ef`

| Name                  | Access | UUID                                   | Format                      |
|-----------------------|--------|----------------------------------------|-----------------------------|
| Service Name          | R      | `ae615002-00ff-4000-8000-0d670255c8ef` | `Configuration`             |
| Maximum Session Time  | RW     | `ae615002-0001-4000-8000-0d670255c8ef` | seconds integer             |
| Maximum Pumping Time  | RW     | `ae615002-0002-4000-8000-0d670255c8ef` | seconds integer             |
| Mode                  | RW     | `ae615002-0003-4000-8000-0d670255c8ef` | string                      |
| Pump Remote Disabled  | RW     | `ae615002-0004-4000-8000-0d670255c8ef` | bool integer                |
| Valve Remote Disabled | RW     | `ae615002-0005-4000-8000-0d670255c8ef` | bool integer                |
| Target Pressure       | RW     | `ae615002-0006-4000-8000-0d670255c8ef` | hPa integer                 |
| Default On            | RW     | `ae615002-0007-4000-8000-0d670255c8ef` | bool integer, stored in NVS |
| Calibration Active    | RWN    | `ae615002-0008-4000-8000-0d670255c8ef` | bool integer                |
| Pressure Deadzone     | RWN    | `ae615002-0009-4000-8000-0d670255c8ef` | hPa integer, stored in NVS  |
| OTA                   | RWN    | `ae615002-000a-4000-8000-0d670255c8ef` | bool integer, runtime only  |

## OTA

Write `1` to the `OTA` characteristic to stop BLE, start the Wi-Fi AP named `OST-RSP`, and enable ArduinoOTA programming.
The default AP password is `VPC-7KQ9-M4W2`. OTA itself has no password.

## Pinout

| Signal            | GPIO |
|-------------------|-----:|
| RF_DATA           |   26 |
| OUT_0 / Pump      |   25 |
| OUT_1 / Valve     |   17 |
| ANALOG / Pressure |   34 |

## Pressure sensor information
Pressure sensor: `XGZP6847A100KPGPN`

| Sensor voltage | Pressure |
|---------------:|---------:|
|          0.5 V | -100 kPa |
|          1.5 V |  -50 kPa |
|          2.5 V |    0 kPa |
|          3.5 V |   50 kPa |
|          4.5 V |  100 kPa |

The analog path is filtered with a `750R/330nF` low-pass filter and divided for 3.3 V ADC logic.

## RF Setup

RF button codes are hardcoded in `include/operation/fw_config.h`.

Open the serial monitor, press remote buttons, and copy logged unknown codes into the button A/B arrays.

## Build

```powershell
pio run
```

## Versioning

This project uses semantic versioning. The current released version is `1.0.2`.

When firmware behavior, BLE values, or user-facing documentation changes, update both the current version above and the newest changelog entry in this file. Keep changelog entries newest first.

## Changelog

### 1.0.2 - 2026-06-14

- Split controller operation-mode logic into separate firmware files:
  - `fw_controller_manual.cpp`
  - `fw_controller_target_oneshot.cpp`
  - `fw_controller_target_hysteresis.cpp`
  - `fw_controller_target_adaptive.cpp`
- Added shared controller common code for pump duty, output application, shutdown, and pressure threshold helpers.
- Kept `fw_controller.cpp` focused on public controller flow, RF handling, timing, calibration, and mode dispatch.

### 1.0.1 - 2026-06-14

- Added README version tracking information.
- Renamed BLE mode values:
  - `automatic_keep` -> `target_hysteresis`
  - `adaptive_keep` -> `target_adaptive`
  - `automatic_single` -> `target_oneshot`
