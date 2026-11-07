# Phase 17E-B — offline-only SRXL2 telemetry polling study

## Hardware evidence (7 November 2026, Pixhawk6C + Spektrum Avian 130A)

Phase 17E-A v183a4942 after successful first-attempt handshake shows:

```
SPK17 SRXL2_LINK_ESTABLISHED
SPK18 FRM H455 C0 T0 O0
SPK18 ESC CAND0 BAD0 OTHER0
```

`H` rises ~98/5 seconds: **these are CRC-valid 0x21 handshake frames, not sensor packets**. This ESC does not send spontaneous `0x80` telemetry in this particular configuration. A successful handshake alone is not enough for telemetry.

## SRXL2 standard

Spektrum open-source `spm_srxl.h` defines:
- `0x21` as handshake and `0x80` as telemetry.
- `0xCD` Control Data with `Command` and `ReplyID`, potentially including *RC channel data*.
- ReplyID selects which device gets to reply with telemetry. Telemetry `0x80` includes *destination but no source ID*.
- Receiver/base master functionality is not generally open in the reference library (master-specific code needs consultation with Spektrum).

See: https://github.com/SpektrumRC/SRXL2 and https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_RCProtocol/spm_srxl.h

## Scope of this branch

Only `tests/test_spektrum_telem_poll_sim.cpp` and the host-test runner are changed.

It produces and inspects a **synthetic host-memory** 14-byte control frame with:
- Packet type `0xCD`.
- Command `0x00` (channel data).
- ReplyID `0x40` (single ESC).
- RSSI/frame losses zero, channel update mask zero.
- No channel update entries.

**CRITICAL: a channel mask of zero is NOT a disarm command, zero-throttle command, nor evidence that the ESC cannot run its motor.** It means no channel values are updated. The ESC may retain a previous demand or interpret the packet differently. Do not put this packet on physical UART4 until a separate qualified motor-safe design is completed, hardware is isolated, and an independent review proves the ESC behavior.

The tests only simulate a single synthetic `0x80` reply addressed to `0x30`. They reject incorrect source-less destination, wrong CRC, wrong sensor ID, unrequested and late replies. A decoded `0x80` is still unauthenticated as to source.

No device firmware code, UART4 transport, motor or reverse control, or arming gates are changed.

## Run the offline tests

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py
```

Expected new line: `7 Phase 17E-B offline-only poll format and sensor reply tests passed`.

## Criteria for a future *separately gated* physical experiment

1. Proven isolated electrical 3.3V-compatible bus and disconnected motor/actuators, with an independently monitored disarmed ESC.
2. Confirmed zero-output/stop semantics from manufacturer documentation or equivalent physical evidence. A zero channel mask is **insufficient**.
3. Specific 0xCD whitelist and absolute TX prohibition on armed states, with watchdog and launch gates independent of user parameters.
4. One request, verified recovery of half-duplex receiver after real TC; no bus contention, no unsolicited 0xCD control sequences.
5. Hard abort on timeout/framing errors/ESC identity mismatch/reconnect, no stale telemetry publication.
6. Explicitly separated bench telemetry proof from flight readiness.

Do not flash this branch for telemetry: this phase is host-only.
