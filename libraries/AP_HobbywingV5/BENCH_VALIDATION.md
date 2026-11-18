# Blade 500X / Pixhawk 4 / Platinum 80A V5: validation plan

**All stages start with no rotor/motor actuation. No flight test until normal airworthiness review.** The integration must not interfere with throttle, governor, failsafe, existing Align RPM sensor or control loop.

## Equipment / evidence

- Pixhawk 4 and exact firmware git SHA, parameter backup, Mission Planner version.
- Platinum 80A V5 model and ESC firmware/software version (record if discoverable without writes).
- TELEM1 cable and official pinout; voltmeter **plus oscilloscope or logic analyzer** (3.0–3.3 V DMM alone does not establish voltage peaks, correct UART waveform or safe electrical compatibility).
- Independent 3.3 V-tolerant serial capture interface if used, appropriately protected.
- Logic trace timestamps, raw hex frames (redact identifiers), decoded CSV, log .BIN, serial error counters, test photos/diagrams.

## Gate A: electrical integrity — NO serial connection until verified

1. Verify official pinout of Pixhawk 4 TELEM1 and Hobbywing P/GND, referencing connector orientation.
2. Confirm ground commonality, 3.3 V IO threshold compatibility, max instantaneous high voltage, idle levels and push-pull/open-drain characteristics using suitable instrument.
3. Leave ESC programmer +V disconnected from Pixhawk UART. Never assume multimeter average proves safe voltage.
4. Confirm TELEM1 is free; do not disconnect needed flight telemetry without understanding consequences.

## Gate B: protocol evidence — throttle disabled / motor phases disconnected

1. Record startup waveform for >10 s from ESC power-on: look for passive 32-byte frames.
2. Confirm serial settings 115200 8N1, polarity, actual packet period.
3. Validate CRC16 and frame type 0x5C30; count valid/invalid frames.
4. If no frames: do not infer the protocol fails. Investigate signal levels, port function, ESC config, and whether non-programming receive requires a wake-up exchange. Do not transmit an unreviewed programming command.
5. Exercise controlled reconnects and reboot orders, maintaining disabled motor actuation.

## Gate C: ArduPilot bench integration

1. Build Pixhawk 4 firmware from pinned commit; record toolchain and binary checksum.
2. Run parser host tests and SITL tests first; verify no regressions in existing ESC telemetry.
3. Use a single UART consumer. Confirm `SERIAL1_PROTOCOL` and serial baud configuration against the compiled driver (protocol 16 alone is not proof).
4. Compare raw-frame values with AP_ESC_Telem and Mission Planner; inspect onboard .BIN and MAVLink telemetry.
5. Compare ESC RPM with independent Align rotor RPM after separately validating pole count and gearing.
6. Test unplugged signal, bad CRC, incomplete frames, ESC off, ESC reboot, serial noise and timeout behavior; stale should be invalid.
7. Check that BEC values are not fabricated from absent fields. If an external BEC powers servos, ESC BEC current does **not** measure external supply load.

## Gate D: controlled powered motor test (only after A–C)

Use an appropriate restrained, qualified test arrangement with blades removed where applicable, independent cutoffs and normal ArduPilot arming safety. No flight while experimental telemetry implementation is unverified. Verify that the telemetry addition changes no throttle timing or failsafe behavior.

## Test records

For every run record: date/time UTC, ESC model, FC board, ArduPilot SHA, driver SHA, parameters, circuit diagram, condition, observed valid/invalid rates, recorded raw and decoded samples, issue links, test outcome, reviewer. Never mark “PASS” without associated logs and reproducible procedure.

## Current measured evidence

- Operator measured `P` signal around 3.0–3.3 V; measurement method/details not yet confirmed.
- Operator reports TELEM1, `SERIAL1_PROTOCOL=16`, `SERIAL1_BAUD=115`, Mission Planner zero values.
- **No verified physical PL5 packets and no proven functional telemetry at this stage.**
