# Phase 18 — physical Spektrum Avian motor turn and full-range integration

## Baseline verified on 2026-11-08
- Branch `feature/spktsmart-bench6-contract`, initial SHA `9657a72ee1`.
- Local host test suite: **135 tests passed**; `build_phase17e_d_poll_lab.sh` returned exit 0.
- Physical link from Pixhawk 6C AUX7/UART4 to Spektrum Avian 130A: `SPK17 SRXL2_LINK_ESTABLISHED`, `ID40 TRY1 OK1 LOST0`; peer 0x21 frames ~20 Hz. One `0xCD` empty-mask poll transmitted (`TX1`) but got no `0x80` reply (`RX0`).
- **NO 6% limit in the normal controller.** The 6% value belongs exclusively to a scripted forward-only motor-turn test.
- ArduPlane final SERVO3 throttle becomes SRXL2 CH1 (zero-based value[0]); explicit ArduPilot reverse direction becomes SRXL2 CH7 (zero-based value[6]); physical SERVO7 is unrelated and must stay usable. MAIN3 PWM throttle must be suppressed when SRXL2 exclusively controls ESC.

## Immediate implementation: separate TurnLab physical firmware
1. Create a dedicated `Pixhawk6C-SpktSmart-TurnLab` hwdef/board/build script; separate from PollLab and standard firmware. Explicitly mark as bench-only/unarmable.
2. Reuse existing UART4 half-duplex implementation, proven AutoLab 0x21 handshake, and TC interrupt handling. Do not add a second UART owner.
3. Add an operator-triggered *one-shot* sequence: explicit encoded neutral CH1 -> bounded +6% CH1 forward demand -> explicit neutral CH1 -> stop and permanent one-shot latch until reboot. This script alone clamps demand at 0.06; normal Motor.cpp keeps 0..1.
4. Do not invent ESC endpoint/reverse values. Confirm the Avian neutral/full encoding and any channel calibration with independent protocol evidence before enabling real nonzero commands. The earlier empty-mask `0xCD` poll is not a neutral instruction.
5. Preserve authorization, arming inhibition, bus arbitration, fresh peer evidence, stale demand protection, fail-closed UART errors, no unexpected restart, and no automatic nonzero command following reboot. Never treat TX TC alone as proof that ESC has applied a command.
6. Emit separate explicit logs (`SPK22`) for real outbound encoded neutral/6%/neutral, TC, RX restoration, peer status and observed response; do not label unsolicited handshakes as telem responses. Distinguish no reply from response outside capture window.
7. Keep motor disconnected until bench owner chooses to connect, without repeating demands for prior BEC isolation. No flight functionality in TurnLab.

## Subsequent: full ArduPlane integration after physical TurnLab
1. `AP_SpektrumSmartESC_Diagnostics::prepare_motor` currently exists but has no caller. Add an explicit, verifiable initialisation path in ArduPlane. Do not just flip compile-time gates.
2. Validate `AP_SpektrumSmartESC_IOOwner` nonce/CRC/readback, and suppression of MAIN3 with all other PWM outputs including SERVO7 unaffected.
3. Use full motor range (`0..1`) with correctly calibrated neutral/full SRXL2 CH1 values. Use reverse function on SRXL2 CH7 only with explicit reverse intent and verified stopped + direction dwell. No global 6% limit.
4. Integrate independent `motor_evidence` ground and stopped timestamps; never fabricate. Check Plane 50 Hz vs worker 1 kHz handoffs.
5. Telemetry must distinguish real verified 0x80 frames from unsolicited 0x21 frames; no invented RPM/current/voltage.
6. Phase acceptance: host tests, builds of PollLab/TurnLab/standard Pixhawk6C, no regression in default output, and explicit physical results of neutral/6%/neutral and later reverse. Do not claim hardware verified on the basis of mock tests.

## Existing code landmarks
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Motor.cpp`, `AP_SpektrumSmartESC_Bench6.h`, `tests/test_spektrum_motor.cpp`
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Diagnostics.cpp` (prepare_motor, motor_evidence, plane_control)
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_ChibiOS.cpp`, `AP_SpektrumSmartESC_IOOwner.cpp`
- `libraries/AP_HAL_ChibiOS/DiagnosticRX.cpp`, `SpektrumUART4.cpp`
- `ArduPlane/servos.cpp`; `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C-SpktSmart-PollLab/`

## Required evidence
- Run `python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py` (old 135 tests + new tests).
- Run `bash libraries/AP_SpektrumSmartESC/tests/build_phase17e_d_poll_lab.sh` (unchanged PollLab); create `build_phase18_motor_turn_lab.sh`.
- Verify no unsanctioned changes to standard target, no general motor-on in PollLab, physical SERVO7 remains independent.
- Report exact SHA and full build/test results; don't overclaim function before physical observations.
