# Upstream proposal and stable-release readiness checklist

## Integration principle

Aim for a narrow, transport-independent V5 decoder with explicit optional serial ownership, supported on Pixhawk6C and other ArduPilot boards. This branch starts at upstream ArduPilot master `8a1dfc1dbdab55f18636feb4784bea4861e2145d`. No DataLink V2 and no motor/ESC UART writes.

## Proposed reviewable patch series

1. Isolated wire format + CRC parser + stream resync and host tests, with provenance.
2. Passive serial receiver with configurable port ownership (currently prototype in AP_BLHeli with SERVO_BLH_HWV5, off by default).
3. Proper telemetry publication, freshness, motor mapping and opt-in diagnostics.
4. Extended fields (BEC voltage/current/temp, motor temp, faults) with approved logging/transport.
5. Model coverage and real hardware evidence; user-facing docs.

## Must pass before an upstream PR is marked ready

- [ ] Host Python and C++11 parser/stream/timing tests pass on clean environment.
- [ ] ArduCopter builds for Pixhawk6C with correct board ID, verified firmware artifact.
- [ ] ArduCopter SITL startup; default-off legacy ESC serial16 regression.
- [ ] Explicit collision check: exactly one consumer of ESC telemetry UART.
- [ ] No ESC UART TX, BLHeli/DShot polling or motor-output modifications in V5 mode.
- [ ] Verified 32-byte CRC-correct physical Hobbywing frames.
- [ ] Test against representative genuine 80A hardware and separately 150A V5.1.
- [ ] Document electrical pinout/waveform, voltage levels and safe connector wiring.
- [ ] Observe full unplug, stale, reconnect and ESC reboot sequences.
- [ ] Standard ESC V/I/temp values compared against independent measurements.
- [ ] Motor/electrical RPM semantics validated with poles and independent tachometer before publishing.
- [ ] BEC extended measurements mapped with correct provenance; no invented mAh values.
- [ ] Parameter naming, reboot semantics, user docs, release notes, minimal memory/flash cost.
- [ ] Confirm correct GPL source attribution/copyright.
- [ ] ArduPilot maintainers review architecture and CI; all changes merged upstream.

## Current evidence as of 2026-11-19

- GitHub branch and initial code exist.
- Host test programs exist, but no remote CI result confirmed.
- No successful Pixhawk6C firmware compile documented.
- No ESC hardware frames or flight tests have been captured.
- An upstream merge or future stable release cannot be claimed.

## Known technical debt in prototype

- AP_BLHeli is a convenience insertion point, not a final approved architectural home.
- No hardware-backed parser fixture or packet-rate vs actual sensor-rate study.
- No full Mission Planner BEC display or governor RPM source integration.
- Need to verify parser resync under pathological byte streams, more robust rate counters, and version-specific compiler/config.
- Need to check baseline `SERVO_BLH_TRATE` and motor mapping behavior across Copter/Plane, and what happens if num_motors==0.
- Better long-term independent driver/serial protocol might be requested upstream.
