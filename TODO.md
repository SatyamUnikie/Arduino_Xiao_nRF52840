# TODO

## Architecture improvement plan

### Current implementation status
- Done: dedicated state layer (`app_state`) introduced.
- Done: dedicated controller layer (`app_controller`) introduced.
- Done: explicit app event bus (`app_events`, `k_msgq`) introduced.
- Done: dedicated LED UI task (`app_ui` thread + queue) introduced.
- Done: deterministic hardware-derived BLE identity module (`app_identity`) introduced.
- Done: BLE device names are now set to `AS-<unique-id>`.
- Done: NFC NDEF Text payload now includes `name=<BLE_NAME>;key=<HEX>` for Android compatibility.
- Next: split telemetry packing/encryption/notify into a separate telemetry module to further reduce controller size.

### Modularity
- Done: create a dedicated application state/controller module to own boot flow, pairing lifecycle, telemetry scheduling, and device-state transitions.
- Done: separate hardware drivers from decision logic by keeping low-level ADC/GPIO/BLE/NFC interactions in drivers while moving app policy rules into the controller/state layer.
- Done: add an identity helper to centralize the hardware-based device naming.
- Introduce a telemetry module to own the plaintext payload definition, encryption flow, and BLE notification pipeline instead of spreading that logic across `main.c`, `ble.c`, and `session_crypto.c`.
- Consolidate board-specific assumptions into one hardware abstraction or configuration header so the app is less coupled to the exact XIAO pin mapping and Zephyr devicetree layout.
- Reduce implicit cross-module coupling by replacing hidden global state with a small runtime context object or explicit module initialization contracts where practical.

### Code maintainability
- Remove repeated ADC-sampling boilerplate from `sensor.c` and `battery.c` by extracting a shared helper for averaging and channel setup.
- Standardize error-handling semantics across modules: distinguish fatal startup failures, recoverable runtime errors, and deferred retries.
- Review naming consistency across modules (`app_*`, `breakwire_*`, `led_*`, `session_*`) and ensure each module owns a single responsibility with a clear API boundary.
- Keep security assumptions explicit: document where the current XOR encryption is temporary, and isolate any future authenticated-encryption migration behind a clear interface.
- Add a module map or architecture note that describes boot lifecycle, NFC trigger flow, BLE pairing flow, sampling loop, and battery policy so new contributors can reason about the app quickly.
- Consolidate configuration values and runtime thresholds into a central source of truth (Kconfig defaults and/or board config), instead of relying on scattered literals and ad hoc tuning.

### Testing strategy
- Add unit tests for pure logic in the app, especially:
  - battery percentage interpolation and voltage-to-percent mapping
  - sensor broken/intact threshold comparison
  - session key generation, XOR encryption, and zeroization behavior
  - LED low-battery hysteresis and base-state transitions
- Add Zephyr/ztest-based verification for state transitions, including:
  - NFC field detection -> pairing start
  - BLE connect/disconnect events
  - session key refresh on disconnect
  - low-battery state toggling under threshold hysteresis
- Introduce mockable hardware interfaces for GPIO, ADC, BLE, and NFC so the app logic can be tested without requiring hardware on every change.
- Add regression tests for the pairing and telemetry flows to ensure future refactors do not break the end-to-end behavior.
- Add CI validation for build and targeted tests to catch regressions before firmware is flashed to hardware.

### Documentation updates
- Expand `README.md` with a clear module responsibility section and runtime flow diagram.
- Keep the low-power roadmap in `TODO.md`, but add explicit validation/acceptance criteria for each architecture change and for any power optimization work.
- Document the current security posture separately from the long-term production design so the temporary XOR scheme remains clearly identifiable as a stop-gap.

### Brief BLE disconnect behavior (intended contract)
- Document that short BLE disconnects are expected and should not be treated as a device unpair or a fatal fault.
- The device should keep its bond and session state during a brief radio drop and attempt automatic reconnect to the previously paired phone.
- Only clear the stored peer bond when an actual security/auth mismatch or stale-bond failure occurs; a transient disconnect should not trigger a pairing reset.
- Notification sends may briefly fail with `-EAGAIN` (`BLE notify deferred: -11`) before the phone enables the CCC descriptor; this is expected behavior and should be treated as a temporary connection/state condition, not as a permanent failure.
- If the phone reconnects successfully within a grace window, the app should resume telemetry without forcing a new pairing flow or resetting the session key unnecessarily.
- If no reconnect occurs after a defined timeout, the app may transition back to advertising or NFC-triggered pairing mode while preserving the bond state until a real auth failure requires cleanup.

## Low power operation plan

### Phase 0 - Baseline and measurement
- Measure current draw in these states:
  - boot / NFC waiting
  - NFC read / BLE advertising
  - BLE connected, idle
  - sensor sample window
  - telemetry transmit
- Record the current firmware and board assumptions.
- Define target current for each state.
- Confirm UICR/NFCPINS is correct before further power work.

### Phase 1 - Reduce always-on work
- Keep sensor MOSFET power gated except during the sample window.
- Keep ADC and battery sampling strictly periodic.
- Keep XIAO battery read path enable (`P0.14`) LOW while USB/charging is possible; do not force it HIGH in this state.
- Treat USB/VBUS-present detection as the primary safety condition for any battery read-path gating optimization.
- Do not rely on `CHG_STAT` alone for gating decisions (`CHG_STAT=HIGH` can still coincide with high battery voltage).
- Implement software USB/VBUS-presence detection (prefer nRF internal VBUS detect as fallback when no dedicated VBUS GPIO is wired) before enabling read-path gating optimizations.
- Avoid any busy loops in application code.
- Ensure all work is scheduled with delayed work or events.
- Review logging level for production use.

### Phase 2 - BLE power reduction
- Tune BLE advertising parameters for low power.
- Tune BLE connection interval and slave latency.
- Minimize radio-on time after pairing.
- Stop pairing advertising after the phone is paired.
- Consider disconnect policy if telemetry is not needed continuously.

### Phase 3 - Peripheral and system PM
- Enable Zephyr power management configuration.
- Allow unused peripherals to suspend.
- Confirm GPIO, ADC, and NFC settings do not prevent sleep.
- Verify the CPU reaches idle/sleep between samples.
- Check wakeup behavior after the sampling timer expires.

### Phase 4 - Application duty cycling
- Reduce sample frequency if the use case allows it.
- Shorten sensor power-on settle time if measurements remain valid.
- Use the minimum useful ADC sample count.
- Batch telemetry updates where possible.
- Avoid unnecessary BLE notifications when values have not changed.

### Phase 5 - Validation and hardening
- Re-measure current after each power change.
- Compare results against the baseline table.
- Verify NFC pairing still works after PM changes.
- Verify BLE telemetry still works after connection tuning.
- Document final power numbers and configuration choices in README.

### Done criteria
- Device idles at the lowest practical current between samples.
- Sensor is powered only during reads.
- BLE and NFC operate correctly without blocking low-power idle.
- Boot logs still report NFC/UICR status for troubleshooting.
