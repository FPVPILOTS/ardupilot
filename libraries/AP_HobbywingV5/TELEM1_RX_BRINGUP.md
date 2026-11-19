# Pixhawk 4 TELEM1 RX bring-up — Hobbywing Platinum 80A V5

**Experimental development branch; not a tested flight firmware.** Purpose: passive listening to the ESC's transmit signal using the RX input of Pixhawk 4 TELEM1. No DataLink V2, no ESC parameter programming, and **no UART TX commands**. This mode is designed to avoid sending any commands or telemetry requests to the ESC.

## What is implemented in this branch

- SerialManager `SERIALx_PROTOCOL=16` uses 115200 baud and a 128-byte RX buffer (the existing 30-byte buffer was smaller than a PL5 32-byte frame).
- `SERVO_BLH_HWV5=1` selects the new receiver branch in `AP_BLHeli::update_telemetry()`; default remains **0** for compatibility.
- Byte-stream framing and recovery from noise/truncation in `Stream.h`, using `Protocol.h` to validate CRC16 and decode fields.
- On valid packets, publishes voltage, current and ESC temperature using `AP_ESC_Telem`. No mAh is invented. RPM is observed in the frame but **not yet published**, pending electrical/mechanical interpretation validation. BEC values are parsed but not yet exposed via MAVLink/standard ESC telemetry.
- Every 5 s, when active on this UART, emit a GCS text diagnostic: `HWV5 RX ok=<n> bad=<n> age=<ms>`; an age of zero before first valid frame is a sentinel, **not** proof of fresh data.
- The legacy BLHeli receiver and DShot request loop are bypassed while this mode is enabled. No write operations in the Hobbywing branch.

## Hardware verification (required before connecting)

1. Obtain official Pixhawk 4 TELEM1 pinout with connector orientation and Hobbywing P/GND pinout. Connect **ESC P signal to TELEM1 RX** and ESC GND to TELEM1 GND, only after checking electrical compatibility with oscilloscope/logic analyzer. **Do not connect programmer +V** to the UART header.
2. DMM reading 3.0–3.3 V alone is not sufficient proof of safe high level or correct UART waveform.
3. Remove blades/disconnect motor phases for initial bench capture; preserve normal motor throttle wiring and arming safety; never run a motor during the first passive-listening test.
4. Ensure TELEM1 is not simultaneously needed for MAVLink radio, telemetry or other device.
5. Take a full ArduPilot parameter backup and record current firmware SHA and board model.

## Firmware preparation — do not flash yet unless builds and review pass

1. Use `feature/hobbywing-platinum-v5-esc-telemetry-20261118` on the `FPVPILOTS/ardupilot` fork.
2. Confirm the code builds for the **correct Pixhawk 4 target** in this historical fork; verify board and bootloader compatibility and review build output/flash size.
3. Run `Tools/hobbywing_v5/test_protocol.cpp`, `test_stream.cpp`, Python tests and other regressions; review CI.
4. Review the patch for changes to motor/servo outputs and UART ownership before any hardware flashing.
5. Do not confuse host tests with a successful ArduCopter build.

## Parameters AFTER building/flashing the verified development firmware

```text
SERIAL1_PROTOCOL = 16         # existing ESC telemetry serial category; TELEM1 on this setup
SERIAL1_BAUD     = 115        # 115200 baud
SERVO_BLH_HWV5   = 1          # NEW PARAMETER, development firmware ONLY; enables passive V5 listening
```

`SERVO_BLH_HWV5` does not exist in official stable firmware. Do not set it on stock firmware. Keep other existing servo/throttle/governor/arming parameters unchanged. Reboot if required for serial initialization.

## Controlled receive-only check

1. Connect FC via USB and open Mission Planner Messages while ESC remains disarmed with motor disconnected.
2. Power the ESC in the approved bench wiring arrangement; observe at least 10 seconds after power-on (Rotorflight uses 5 s boot delay).
3. Look for `HWV5 RX ok=... bad=... age=...ms`. **If no HWV5 message appears:** firmware or parameter not active, no configured UART, or the update hook not called; this is not yet evidence about the ESC signal.
4. **If `ok` increases:** valid 32-byte CRC-checked PL5 frames have arrived. Check voltage/current/temperature via logs and available ground-station telemetry.
5. **If `ok=0` and `bad=0`:** no complete candidate frames; check TX signal waveform, ground, pins, UART RX, passive transmission and baud/inversion.
6. **If `bad` increases but `ok=0`:** stream sees candidate frame starts but fails validation; capture raw bytes before changing protocol assumptions.
7. Unplug signal (ESC disarmed) and verify data staleness and return after reconnection; record raw captures and full log .BIN.

## Evidence to preserve

- Firmware Git SHA, build/CI logs and board target.
- Screenshot of parameter values and Mission Planner messages.
- Time-stamped raw serial capture and CRC verification.
- ESC hardware type, firmware version and supply voltage.
- Valid/invalid counter changes, stale behavior and comparison with independent instrument.
- Record both successful and unsuccessful attempts; never mark hardware PASS based on simulation.

## Known limitations

- First backend is attached to existing `AP_BLHeli` in this **older fork**, not yet on a clean upstream base. Upstream API compatibility must be addressed before official PR.
- Existing motor channel mapping must be verified on the Blade 500X; if no ArduPilot motor output is mapped, this backend currently does not claim the serial port and cannot listen.
- Runtime parameter switching has not been validated; set parameter and restart for controlled tests.
- Passive mode assumes the physical ESC sends telemetry unprompted. A quiet RX line does **not** justify sending programming commands; check exact ESC behavior first.
- No compiled Pixhawk 4 firmware, no SITL validation and no physical-frame evidence has been produced yet.

## Why no live RX claim yet?

Only code on the fork's new branch was modified. Until a verified build is flashed and physical serial frames are captured from TELEM1, the RX pin is **not known** to be receiving Hobbywing data.

## Frame-rate diagnostic added (2026-11-19)

Valid Hobbywing frames are stamped immediately when fully decoded with `AP_HAL::micros()`. The 5-second GCS report only summarizes these events; it does **not** throttle receive or ESC telemetry updates.

Two messages (illustrative only, not hardware evidence):

```text
HWV5 RX ok=123 bad=1 age=75ms
HWV5 RX Hz10=100 dt=100000 max=120000 us
```

- `Hz10`: 10 × estimated frames per second, e.g. 100 = 10.0 Hz (exponential moving average of valid-frame interarrival intervals).
- `dt`: interval in microseconds between the two most recent valid frames.
- `max`: maximum observed interval in microseconds since boot.
- `age`: milliseconds since last valid frame; `4294967295` indicates no valid packet observed.
- A frame rate is a **receive** rate, not a measurement of internal ESC sensor update rate or governor suitability.
- Freshness threshold in test helper: 500 ms by default. Standard AP_ESC_Telem freshness handling still needs end-to-end validation.

Current firmware integration publishes voltage/current/ESC temperature on each accepted frame, **not** every five seconds; RPM remains deliberately un-published until scale and timing are verified with the independent Align RPM sensor.

Host regression `Tools/hobbywing_v5/test_timing.cpp` covers sample age, 10 Hz interval calculation, stale status and 32-bit microsecond timer wrap. GitHub Actions workflow is configured to run every `test_*.cpp` file. Remote CI/build results are not yet verified; do not flash for flight.
