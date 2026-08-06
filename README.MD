<div align="center">
  <h1>Smart Remote Vacuum Pump</h1>
  <img src="https://github.com/H1M4W4R1/RemoteVacuumPump/blob/master/images/pump.webp?raw=true" alt="Device photo"/>
</div>

This project is a direct upgrade of previous one available <a href="https://github.com/H1M4W4R1/RemoteVacuumPump">here</a>.
Firmware controls a normally-open valve and three parallel 555-type pumps using cheap -100 to 100kPa pressure sensor as feedback.
Pumps can be controlled using 433MHz ASK remote or using BLE.

Current version: `2.0.3`

## Controls

Button A controls the pump:

- If the pump is off, button A starts the pump.
- In manual mode, button A also locks the valve when starting the pump.
- In automatic modes, button A does not change valve state.
- In `target_hysteresis` and `target_adaptive`, button A follows the virtual controller state, so it suspends the controller even when the physical pump is currently idle at target pressure.
- In `current_hysteresis` and `current_adaptive`, button A starts preload when the physical pump is off and captures the current pressure when stopping the pump.
- In target modes, if the pump or virtual controller is running, button A stops it and marks the pump as temporarily disabled.
- In target modes, pressing button A again manually reactivates the pump and clears temporary disable.

Button B controls the valve:

- Button B toggles the valve lock.
- Button B is ignored when valve remote control is disabled.
- Opening the valve stops the pump in every mode, including `manual`.
- Automatic target and current modes only run the pump while the valve is locked.
- Unlocking the valve while the pump is running immediately puts the pump on hold.

## Modes

`manual` lets the user control pump and valve directly.

`target_oneshot` stops pumping when target pressure is reached. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `target_oneshot`.

`target_hysteresis` stops pumping when target pressure is reached and restarts when pressure moves outside the configured deadzone. If the user manually disables the pump, the firmware waits for manual reactivation before continuing in `target_hysteresis`.

`target_adaptive` uses PWM pump drive with an adaptive PID loop to hold the configured target pressure. It continuously estimates chamber response while pumping and leak/drift while idle, then adjusts pump duty at runtime for chambers that vary between usages.

`current_hysteresis` works like `target_hysteresis`, but it uses the pressure captured when the pump is stopped over BLE or RF instead of the configured target pressure.

`current_adaptive` works like `target_adaptive`, but it uses the pressure captured when the pump is stopped over BLE or RF instead of the configured target pressure.

Current modes let the user preload the chamber to the desired pressure, stop the pump, and let firmware maintain that captured pressure while the valve remains locked. If the mode is selected while the pump is already stopped, the firmware captures the current pressure once.

## Calibration

Set `Calibration Active` to `1` over BLE when the valve is open and the sensor is reading atmospheric pressure. The firmware turns pump and valve outputs off before sampling, computes the divider multiplier from the 2.5 V zero-pressure point, stores it in NVS, and clears `Calibration Active`.

If no calibration exists in NVS, the firmware attempts auto-calibration on boot.

## BLE

BLE device name: `OST-RSP`

The firmware uses BLECommand: one service with the three command endpoints below. The legacy Session and Configuration services are removed.

| Endpoint | Access | UUID | Purpose |
|----------|--------|------|---------|
| TX | Write, Write Without Response | `40300002-0023-4bd4-bbd5-a6920e4c5653` | Commands sent to the device |
| RX | Read, Notify | `40300003-0023-4bd4-bbd5-a6920e4c5653` | Command replies |
| NX | Notify | `40300004-0023-4bd4-bbd5-a6920e4c5653` | Live state events |

Service UUID: `40300001-0023-4bd4-bbd5-a6920e4c5653`

Every endpoint value is ASCII. Subscribe to RX for replies and NX for live events. BLECommand also exposes a read-only `Service Name` metadata characteristic at UUID `0x00FF`.

Write TX packets in the form `Command:data;`. Commands are case-insensitive; the documented PascalCase form is preferred. BLE writes may split a packet or include multiple packets. A command without data queries its current value, for example `Mode;` returns `Mode:manual;`. A successful command with data returns `OK;`.

`automatic` is accepted as an alias for `target_hysteresis`; queries always return the canonical mode name. Boolean values are `0` or `1`, time values are whole seconds, and pressure values are whole hPa.

| Command | Set form | Read reply | Accepted value |
|---------|----------|------------|----------------|
| `Mode` | `Mode:automatic;` | `Mode:[mode];` | `manual`, `automatic`, `target_hysteresis`, `target_oneshot`, `target_adaptive`, `current_hysteresis`, or `current_adaptive` |
| `Pump` | `Pump:1;` | `Pump:[0\|1];` | `0` or `1` |
| `Valve` | `Valve:1;` | `Valve:[0\|1];` | `0` or `1` |
| `CurrentPressure` | `CurrentPressure:-50000;` | `CurrentPressure:[hPa];` | Signed hPa integer |
| `MaxSession` | `MaxSession:28800;` | `MaxSession:[seconds];` | Non-negative seconds integer |
| `MaxPumping` | `MaxPumping:1500;` | `MaxPumping:[seconds];` | Non-negative seconds integer |
| `PumpRemoteDisabled` | `PumpRemoteDisabled:1;` | `PumpRemoteDisabled:[0\|1];` | `0` or `1` |
| `ValveRemoteDisabled` | `ValveRemoteDisabled:1;` | `ValveRemoteDisabled:[0\|1];` | `0` or `1` |
| `TargetPressure` | `TargetPressure:-50000;` | `TargetPressure:[hPa];` | Signed hPa integer |
| `DefaultOn` | `DefaultOn:1;` | `DefaultOn:[0\|1];` | `0` or `1` |
| `CalibrationActive` | `CalibrationActive:1;` | `CalibrationActive:[0\|1];` | `0` or `1` |
| `PressureDeadzone` | `PressureDeadzone:50;` | `PressureDeadzone:[hPa];` | Integer `0` to `1000` hPa |
| `SessionTime` | — | `SessionTime:[seconds];` | Read only |
| `PumpTime` | — | `PumpTime:[seconds];` | Read only |
| `LastSessionTime` | — | `LastSessionTime:[seconds];` | Read only |
| `TotalSessionTime` | — | `TotalSessionTime:[seconds];` | Read only |
| `MinimumPressure` | — | `MinimumPressure:[hPa];` | Read only |
| `MaximumPressure` | — | `MaximumPressure:[hPa];` | Read only |

While connected, NX emits an individual event only when one of these live values changes:

```text
Pump:[0|1];
Valve:[0|1];
SessionTime:[seconds];
PumpTime:[seconds];
LastSessionTime:[seconds];
TotalSessionTime:[seconds];
CurrentPressure:[hPa];
MinimumPressure:[hPa];
MaximumPressure:[hPa];
```

Configuration values are never sent through NX. Query their commands to receive their current values through RX.

### Errors

All errors are returned through RX as `ERR:error_type;`.

| Error type | Meaning |
|------------|---------|
| `invalid_arguments` | The command has an unsupported number of data fields. |
| `invalid_value` | The value is not valid for the command. |
| `out_of_range` | A numeric value is outside the allowed range. |
| `operation_failed` | The requested controller or persistent-storage action failed. |
| `unknown_command` | The BLECommand transport received an unregistered command name. |
| `packet_too_long` | The BLECommand transport discarded a packet above its fixed 256-byte limit. |

BLECommand accepts packets up to 256 bytes, including the terminating semicolon, with up to 16 data fields.

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

This project uses semantic versioning. The current released version is `2.0.4`.

When firmware behavior, BLE values, or user-facing documentation changes, update both the current version above and the newest changelog entry in this file. Keep changelog entries newest first.

## Changelog

### 2.0.4 - 2026-08-06

- Replaced periodic NX `Status` and `Config` batches with individual live-value events emitted only when a value changes.
- Stopped sending configuration values through NX; configuration queries return their values through RX.

### 2.0.0 - 2026-08-06

- Replaced the two-service, multi-characteristic BLE interface with the BLECommand single-service TX/RX/NX command transport.
- Added command replies (`OK;`), queries, documented error replies (`ERR:error_type;`), and one-second NX `Status` and `Config` events.
- Added `automatic` as a BLE command alias for `target_hysteresis`.

### 1.2.0 - 2026-06-14

- Removed ArduinoOTA support and its Wi-Fi access point configuration.
- Removed the BLE `OTA` characteristic from the Configuration service.

### 1.1.1 - 2026-06-14

- Documented valve safety for `manual` mode: opening the valve stops the pump in every mode.

### 1.1.0 - 2026-06-14

- Added `current_hysteresis` mode, which captures the current pressure on BLE/RF pump stop and maintains it with hysteresis.
- Added `current_adaptive` mode, which captures the current pressure on BLE/RF pump stop and maintains it with adaptive PWM control.
- Added automatic-mode valve safety: target and current modes hold the pump while the valve is unlocked.
- Unlocking the valve while the pump is running now immediately puts the pump on hold.
- Updated BLE mode values documentation with the new current modes.

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
