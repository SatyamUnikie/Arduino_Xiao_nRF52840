# Android app developer TODO for Wire Sensor UX

This document describes the Android-side behavior needed to support the intended user experience for the Wire Sensor system.

## Purpose

The board is a BLE peripheral with NFC-based pairing and a simple sensor telemetry flow. The Android app should make the pairing and session setup feel seamless while staying compatible with the board’s current firmware behavior.

## High-level UX

The end-user experience should be:

1. User taps the phone to the XIAO NFC tag area.
2. The app detects the NFC tag or NDEF payload and starts a secure pairing flow.
3. The board starts BLE advertising automatically.
4. The app discovers the board over BLE and pairs it.
5. The app receives encrypted sensor telemetry over BLE after the session is established.
6. The app presents the sensor state, battery level, and connection status in a simple UI and sends the data to web UI.

## Board behavior to match

### Temporary BLE disconnect / reconnect contract

The board is expected to behave like a modern paired mobile peripheral:

- a short BLE disconnect should not be treated as a full unpair event
- the bond and session state should remain valid during a brief radio drop
- the app should reconnect to the previously paired phone automatically within a short grace window
- if the BLE CCC descriptor is not yet enabled, early notify attempts may fail with a temporary `-EAGAIN`/deferred condition; this should be handled as expected startup behavior, not as a protocol failure
- only real pairing/authentication errors or stale-bond states should clear the peer bond and force a fresh pairing cycle
- if reconnect succeeds, sensor telemetry should resume without requiring a new NFC read or device re-selection

The Android app should therefore distinguish between:
- transient connection loss
- peer bond cleanup required due to auth mismatch
- a true lost device / user-initiated unpair

### 1) NFC tag behavior

The board exposes an NFC Type 2 Tag interface through the nRF52840 NFCT peripheral.

The firmware currently:
- starts NFC emulation on boot
- refreshes the NDEF payload and restarts emulation
- waits for an NFC field
- triggers BLE advertising when an NFC read event occurs
- uses the NFC read as the pairing gate and session-key source
- publishes a valid NDEF Text record containing both the BLE name and the session key

NFC payload format:
- `name=AS-<unique-id>;key=<HEX>`
- example: `name=AS-3A4F91D8C2B761E2;key=ABCD1234EF5678901234567890ABCDEF`

Android app requirements:
- The app must listen for NFC tag discovery using the Android NFC foreground dispatch or an NFC reader flow.
- It should process the tag if it contains the expected NDEF payload and parse the `name` field and `key` field.
- The app must not require the user to manually enter a PIN or pairing code.
- Once the NFC tag is read, the app should immediately begin scanning for the BLE device whose advertised name matches the parsed value from the `name=` field.

Expected sequence:
- NFC field detected by the board
- board starts BLE pairing advertising
- Android app receives the NFC reading and parses the `name` and `key` values
- app scans for a BLE device whose name exactly matches the parsed `name` value
- app connects to the matching board and continues the BLE pairing flow

### 2) BLE pairing sequence

The board advertises as a BLE peripheral and is intended to be paired by a phone app.

Current firmware behavior:
- advertising is started from NFC trigger
- BLE advertising stops when the phone connects
- the board is configured as a Bluetooth peripheral with Security Manager enabled
- the NFC exchange provides the session key and the app uses this as the pairing/session initialization step
- the BLE device name is the unique derived sensor ID in the form `AS-<unique-id>`
- if pairing fails with a security-requirement/key-missing mismatch, the board clears the stale bond for that peer and disconnects; a second attempt is expected to succeed

Android app requirements:
- Use standard Android BLE scanning and GATT connection flow.
- On discover, connect to the board when the device name matches the parsed `name=` value in the NFC tag.
- If the board is using bondable pairing and SMP, the app should allow standard Android pairing to complete.
- If the first pairing attempt fails, automatically retry once after reconnect (do not force the user through repeated manual retries).
- Once connected, the app should subscribe to relevant GATT characteristics if the service UUIDs are exposed by the board.
- The app should handle reconnect and re-pair gracefully.


### 3) Battery and sensor service expectations

The board reports:
- battery level via the BLE Battery Service (BAS)
- sensor telemetry through encrypted notifications

Android app requirements:
- Read battery level from the Battery Service Characteristic if present.
- If the board reports `batt=0%`, treat this as a valid state when the battery is disconnected or missing.
- Treat the reading as a low-battery or no-battery condition, not a protocol error.
- For sensor data, subscribe to the notified characteristic and decrypt payloads using the session key established during NFC + pairing flow.

### 4) Session key and encrypted traffic

Current implementation notes:
- NFC provides a temporary session key
- BLE payloads are XOR-encrypted using the session key
- the key is zeroized on reconnect/bootstrap paths
- this is a temporary crypto scheme and will eventually be replaced with a more robust authenticated encryption flow

Android app requirements:
- Persist the session key from the NFC exchange or derived pairing material for the active session.
- Apply the same XOR transformation when decrypting payloads on the app side.
- Be prepared for session reset or re-init after reconnects.
- Reject invalid payloads or missing session keys without crashing the app.

## Required Android implementation tasks

### A. NFC handling

- Add NFC intent filtering for the expected tag type or custom NDEF data.
- Implement a foreground dispatch so NFC reads can be captured while the app is open.
- Parse the NFC payload to identify the board and extract both `name` and `key` values.
- Trigger pairing flow immediately after tag detection.
- Match the device using the parsed value from `name=AS-<unique-id>` instead of a single generic name.
- Handle cases where the tag is not recognized or is stale.

### B. BLE scanning and connection

- Add a BLE scan filter for the exact board name parsed from the NFC tag (for example `AS-3A4F91D8C2B761E2`) or the advertised service UUID.
- Start scanning after NFC read is detected.
- Connect to the device with a stable device address or stable service discovery.
- Monitor connection state changes and show a clear UI status indicator.
- Handle reconnects by re-subscribing to GATT notifications.

### C. GATT subscription and telemetry parsing

- Discover the relevant service and characteristic UUIDs.
- Enable notifications once connected.
- Treat an initial lack of telemetry right after connect/pair as normal until CCC subscription is complete; firmware may defer notifications until then.
- Receive the telemetry notifications and validate payload length and format.
- Decrypt using the active session key.
- Convert raw sensor values into a user-friendly state such as:
  - intact
  - broken / tripped
  - disconnected battery
  - unknown / invalid sample

### D. UX and user feedback

The app should provide clear states to the user:
- scanning for board
- NFC tag detected
- BLE connecting
- pairing in progress
- paired and connected
- sensor reading available
- battery unavailable / 0%
- disconnected or fault state

Suggested UI states:
- `Waiting for NFC`
- `Board detected`
- `Connecting to Wire Sensor`
- `Pairing`
- `Connected`
- `Sensor OK`
- `Sensor Open / Broken`
- `Battery missing`
- `Connection lost`

### E. Failure handling

The Android app should gracefully handle:
- NFC tag not detected
- BLE device not found
- pairing rejected by the board
- session key missing
- malformed telemetry payload
- battery disconnected / 0%
- lost connection after initial pairing

## Required sequence diagram

```text
User taps phone to NFC tag
        |
        v
Android app reads NFC tag
        |
        v
App starts BLE scan
        |
        v
Board emits BLE advertising after NFC trigger
        |
        v
App connects to board
        |
        v
Android app completes pairing / SMP flow
        |
        v
Session key / encrypted telemetry path is active
        |
        v
App reads battery data and sensor notifications
        |
        v
UI updates with current sensor state
```

## Important caveats for Android development

- The board is currently a BLE peripheral plus NFC pairing gate. The app should assume the flow is NFC-first, BLE-second.
- The board advertises only while pairing is active or after NFC-triggered activation.
- Sensor payloads are encrypted with a temporary XOR scheme, so the app must not assume plain-text data.
- The app should treat `0% battery` as a valid, explicit runtime output from a disconnected battery, not as a crash condition.
- The app should support the possibility that the board is not connected at the moment a sensor event is read.
- Android may show a stale cached BLE name from an earlier scan or earlier firmware revision, even when the live local name is different. The authoritative source for pairing should be the exact NFC payload value (`name=AS-<unique-id>`) and the current device scan result, not an old cached display label.
- If the phone shows `WireSensor` while the device is currently advertising `AS-9DD1564711CC8BAB`, the app should refresh the scan and prefer the exact NFC name match over any stale cached name.

## Definition of done

The Android app is considered ready when:

- NFC tag discovery triggers the pairing flow
- the app can discover the Wire Sensor BLE peripheral
- BLE pairing succeeds reliably
- battery values are read from the BLE Battery Service
- encrypted sensor notifications are decrypted and displayed
- sensor state is shown to the user in a clear UI
- reconnect and retry logic is implemented
- the app handles low/no battery and missing session states without crashing

## Recommended next implementation order

1. NFC tag detection and foreground dispatch
2. BLE scan + connection on NFC trigger
3. GATT notification subscription
4. Decrypt sensor payloads
5. Battery service read logic
6. UI state handling and error states
7. Reconnect / retry flow

## Notes

This document reflects the current firmware design and should be updated when the board moves to a more permanent authenticated-encryption scheme or when the BLE service layout changes.

For iOS-specific discovery and CoreBluetooth integration guidance, see `IOS_TODO.md`.
