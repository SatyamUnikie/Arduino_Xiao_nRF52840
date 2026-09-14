# wire-sensor

Zephyr/NCS scaffold for a Seeed XIAO nRF52840 breakwire monitor.

## Project summary

The firmware is structured to:
- power a breakwire sensor through a MOSFET enable pin
- read the sensor through an analog input every 2 seconds
- report sensor status and battery level over BLE
- use NFC as the pairing gate and session-key source
- keep the device low power between sample cycles
- route runtime behavior through a dedicated app controller and event bus

Battery power source:
- single-cell 3.7V LiPo
- 400mAh capacity
- battery level is estimated from LiPo voltage range
- when the battery is absent or disconnected, the app reports `batt=0%` and continues sampling

The current crypto flow is intentionally temporary:
- NFC provides a 128-bit session key
- BLE payloads are XOR-encrypted with that key
- the key is zeroized on boot/reconnect paths
- the current session key is also printed to UART in HEX (`SESSION_KEY_HEX:<32-hex-chars>`) for bring-up/debug
- the NFC payload is a valid NDEF Text record containing both the stable BLE identity and the session key

The sensor identity is derived from the hardware unique ID and formatted as a fixed BLE name:
- `AS-<16-hex-char unique ID>`
- example: `AS-3A4F91D8C2B761E2`
- Android should match the name exactly when scanning for the sensor after an NFC read

## Current architecture

The application is split into four layers:

1. App orchestration layer
   - `src/main.c`: thin bootstrap that starts the app controller.
   - `src/app_controller.c`: central orchestration for initialization, state transitions, sensor sampling, BLE/NFC lifecycle, and telemetry transmission.
   - `src/app_state.c`: runtime state ownership (pairing, connection, pairing-complete, low-battery, fault).
   - `src/app_identity.c`: deterministic hardware-derived device identity and BLE name generation.

2. App messaging and UI layer
   - `src/app_events.c`: explicit event bus implemented with `k_msgq` (`APP_EVENT_*` messages).
   - `src/app_ui.c`: dedicated UI thread that consumes UI commands and updates LEDs.
   - `src/led_status.c`: hardware-facing LED driver/pattern implementation.

3. Hardware/service modules
   - `src/sensor.c`: breakwire ADC acquisition and threshold decision input.
   - `src/battery.c`: battery ADC acquisition and LiPo OCV interpolation.
   - `src/ble.c`: BLE GATT service, connection/pairing callbacks, notifications, BAS updates, and dynamic device name setup.
   - `src/nfc_pairing.c`: NFC Type 2 Tag setup and NDEF text payload refresh with sensor name + session key.
   - `src/session_crypto.c`: session key generation/zeroization and XOR transform.

4. Identity and pairing metadata
   - `src/app_identity.h`: public identity API and name format contract.
   - `src/app_identity.c`: derives the unique board identity from the hardware EUI/ID and formats the BLE name as `AS-<unique-id>`.

### Runtime flow

- NFC field/read activity triggers NFC callback.
- NFC callback posts `APP_EVENT_NFC_TRIGGER`.
- App controller handles the event, starts BLE advertising, updates app state, and sends LED UI commands.
- BLE callbacks are converted into controller events (`CONNECTED`, `DISCONNECTED`, `PAIRED`) and handled in one place.
- Periodic sampling is scheduled via delayed work, converted into `APP_EVENT_SAMPLE_TICK`, and processed by the controller.
- Advertising timeout is also posted as an event (`APP_EVENT_ADV_TIMEOUT`) so timeout behavior stays in the same control path.
- LED changes are not made directly from business logic; business logic posts UI commands, and the UI thread applies LED effects.

## Hardware configuration

Target board:
- Seeed XIAO nRF52840

Logical pin usage:
- `D0` / board pin 0: MOSFET gate to power the breakwire sensor
- `D1` / `A1` / board pin 1: analog input for sensor line readout
- `D14` / `P0.14`: battery read-path enable (active low)
- `P0.31` / `AIN7`: battery voltage ADC input via the board's internal divider

Devicetree assumptions:
- `breakwire-power-gpios` is active-high
- `battery-read-enable-gpios` is active-low (`P0.14`)
- `io-channels` contains two ADC channels:
  - channel 1: breakwire sensor input on `AIN1`
  - channel 7: battery input on `AIN7`
- `&nfct` is enabled in the app overlay so the nRF52840 NFCT peripheral is available to the NFC library

ADC assumptions:
- 12-bit SAADC resolution
- internal reference
- gain set to `ADC_GAIN_1_6`
- battery ADC voltage is scaled back to battery voltage using the internal divider ratio (default 1510/510)

## Software behavior

- sensor sampling interval: 2000 ms
- ADC averaging uses 8 samples by default (configurable from 5 to 10)
- breakwire decision threshold: 50% of ADC full scale by default
- LiPo battery profile: 400mAh, 3.7V nominal, 4.2V full, 3.3V empty
- battery percentage is calculated using a non-linear LiPo OCV profile (with interpolation), not a simple linear empty/full mapping
- BLE peripheral mode enabled
- Battery Service enabled
- NFC pairing is started by NFC read/field activity
- the NFC payload is a valid NDEF Text record with the form: `name=AS-<unique-id>;key=<HEX>`
- Android should parse this exact structure and use `name` as the BLE device name to search for and connect to the correct sensor
- after successful pairing/connection, pairing advertisements stop
- boot logs print the UICR.NFCPINS value so you can confirm NFC pins are still enabled
- boot/reconnect key refresh prints `SESSION_KEY_HEX:<32-hex-chars>` to UART for debug visibility
- BLE advertising times out after 60 seconds if no connection is made
- the BLE device name is set to the same unique identity reported on the tag: `AS-<unique-id>`
- advertising includes a shortened local name in the primary ADV payload and the full name in scan response to improve cross-platform discovery behavior
- NFC-triggered pairing is latched while pairing is in progress, so repeated reads do not restart it
- if BLE security upgrade fails with stale/mismatched bond state (`BT_SECURITY_ERR_AUTH_REQUIREMENT` or `BT_SECURITY_ERR_PIN_OR_KEY_MISSING`), the firmware clears the stored bond for that peer and disconnects so a second pairing attempt can succeed cleanly
- sample logs report sensor state, battery percentage, and BLE connection/pairing state for live debugging
- before the phone subscribes to notifications (CCC not enabled yet), encrypted telemetry sends may be deferred with `-EAGAIN` (`BLE notify deferred: -11`); this is expected and stops after the client enables notifications on the sensor characteristic
- if the battery is disconnected, the app continues to run and reports `batt=0%` instead of failing the application
- low battery indication uses hysteresis: LED starts blinking red below `CONFIG_APP_BATTERY_LOW_PERCENT` (default 10%) and stops at/above `CONFIG_APP_BATTERY_LOW_CLEAR_PERCENT` (default 14%)
- conservative battery safety policy keeps `P0.14` active (LOW) by default, including when USB is connected
- optional USB-aware optimization can disable the read path between samples when USB power is absent
- controller/event-bus design keeps BLE/NFC callbacks lightweight and centralizes app behavior in `app_controller`

## Brief BLE disconnect behavior

Short BLE drops are expected and should not be treated as a device unpair or a fatal fault. The intended contract is:

- a brief disconnect should keep the bond and session state alive
- the sensor should retry reconnect to the previously paired phone within a short grace window
- `BLE notify deferred: -11` before CCC subscription is enabled is expected and transient, not a permanent failure
- only actual authentication/security failures or stale-bond mismatches should trigger peer-bond cleanup and a fresh pairing cycle
- if the phone reconnects quickly, the app should resume encrypted telemetry without forcing a new NFC or pairing flow
- if no reconnect arrives within the configured timeout, the device may return to advertising or NFC-triggered pairing mode while preserving the bond until a real auth failure requires cleanup

This matches the modern “headphones” behavior for a paired mobile device: the app should treat temporary radio disconnects as a transient transport issue rather than a full user-visible unpair event.

## iOS BLE discovery note

- iOS can read the NFC tag payload, but custom BLE peripherals may not appear in the iOS Settings Bluetooth device list.
- For verification and bring-up, use a BLE scanner app such as LightBlue or nRF Connect on iPhone.
- Production iOS apps should discover and connect with CoreBluetooth (scan by service UUID and/or `name=AS-<unique-id>` from NFC), then subscribe to notifications.

## LED status behavior

- solid green: no NFC seen yet
- blinking green: NFC tag read
- blinking blue: BLE advertising and connection establishment
- solid blue: BLE paired / stable
- red blink: runtime fault such as sensor read, battery read, or BLE notify failure
- red continuous blink: low battery (hysteresis thresholds: on below `CONFIG_APP_BATTERY_LOW_PERCENT`, off at/above `CONFIG_APP_BATTERY_LOW_CLEAR_PERCENT`)

## Assumptions

- The breakwire is powered only during the read window.
- Sensor and battery reads both average multiple ADC samples before making a decision / converting to percentage.
- During charging-capable conditions, the battery read path should not be disabled on XIAO nRF52840 (`P0.14` must stay LOW).
- NFC is used as a pairing gate, not as the final production security layer.
- If the board/UICR was previously configured to treat NFC pins as GPIOs, those UICR settings must be cleared for NFC to work.
- At startup, the firmware reports whether `UICR.NFCPINS` is set for NFC mode or still disabled as GPIO.
- XOR encryption is temporary and will be replaced by proper authenticated encryption later.
- Session-key UART HEX output is for development/bring-up only and should be disabled or gated before production.
- The build target is `xiao_ble` in the current NCS release.

## Build

Build from the NCS workspace root with this app as the source tree:

```bash
west build -p always -b xiao_ble -s C:\Users\tymur\Documents\Private\Armor\Sensor\wire-sensor
```

The project also includes a local Windows helper script:

```bat
build.bat
```

This builds the firmware for the `xiao_ble` target and writes the UF2 output into the project-local `build/zephyr/zephyr.uf2` path.

## Notes

- The real project source lives in `C:\Users\tymur\Documents\Private\Armor\Sensor\wire-sensor`.
- Other Zephyr workspaces were only used as temporary build roots during validation.
