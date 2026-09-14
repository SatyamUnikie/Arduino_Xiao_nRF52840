# iOS app developer TODO for Wire Sensor BLE flow

This document captures the minimal iOS-specific flow that matches the current firmware behavior.

## Important platform behavior

- The sensor is a custom BLE peripheral. It might not appear in iOS Settings -> Bluetooth.
- NFC tag reads can work even when the device does not show in Settings.
- Use CoreBluetooth scanning/connection in app code, or use LightBlue/nRF Connect for validation.
- Brief BLE disconnects are expected and should not be treated as a full unpair event.
- The app should allow reconnect to the previously paired device instead of forcing the user to re-pair after a short radio drop.
- Before the device enables the notification CCC descriptor, early GATT notifications may be deferred; this should be treated as a normal temporary condition.

## Minimal CoreBluetooth checklist

1. Start NFC read flow and parse NDEF text payload:
   - expected format: `name=AS-<unique-id>;key=<HEX>`
   - extract `name` and `key`
2. Start `CBCentralManager` scan immediately after NFC read:
   - filter by service UUID `11223344-5566-7788-99AA-BBCCDDEEFF01` when possible
   - also match `CBAdvertisementDataLocalNameKey` to parsed `name`
3. Connect to matched peripheral with `connect(_:options:)`.
4. Discover services and characteristics:
   - service UUID: `11223344-5566-7788-99AA-BBCCDDEEFF01`
   - notify characteristic UUID: `11223344-5566-7788-99AA-BBCCDDEEFF02`
5. Subscribe with `setNotifyValue(true, for:)`.
6. Wait for notification state callback before expecting telemetry:
   - before subscription is active, firmware may defer sends (`-EAGAIN` on device side)
7. In `didUpdateValueFor`, decrypt payload using session key from NFC and parse telemetry fields.
8. Read BAS battery level from standard Battery Service (`180F`/`2A19`) if needed.
9. Implement one automatic reconnect/retry on first pairing failure:
   - firmware may clear stale peer bond and require second attempt.

## Recommended runtime states in the iOS app

- Waiting for NFC
- NFC tag parsed
- Scanning
- Connecting
- Pairing/Securing link
- Subscribing notifications
- Receiving telemetry
- Reconnect/retry

## Validation steps

1. Confirm NFC read yields `name` and `key`.
2. Confirm scanner app (LightBlue or nRF Connect) can see `AS-<unique-id>`.
3. Confirm app can connect and subscribe.
4. Confirm telemetry starts after CCC subscription.
5. Confirm first-failure-second-success pairing recovery path works.
