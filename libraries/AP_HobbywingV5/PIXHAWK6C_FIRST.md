# Pixhawk 6C first-build decision (2026-11-19)

## Decision

Prioritize **Holybro Pixhawk 6C** as the first compile and receive-only test target, ahead of Pixhawk 4. The radio/FC being tested must be recorded separately from the ESC and vehicle; initial hardware purpose is only to receive Hobbywing Platinum V5 UART telem frames. Do not assume the Blade 500X's Pixhawk 4 has been swapped for a 6C or that the 150A is selected instead of the 80A.

## Critical board availability finding

The checked **FPVPILOTS/ardupilot** fork's default branch does **not** contain `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C/hwdef.dat` (GitHub contents API returned 404 on 2026-11-19). **Official current upstream ArduPilot master does contain** `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C/hwdef.dat`:

https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C/hwdef.dat

Therefore DO NOT assert that this legacy branch can compile a Pixhawk6C target. The actual next dependency is to transplant/rebase the work onto a **clean current upstream-based branch** and resolve ESC telemetry API and serial consumer differences before building. Avoid overwriting the existing legacy branch or merging it blindly. Recheck if the user wants to use a different custom 6C board definition.

## Proposed next validation gates

1. On the development machine, fetch latest upstream `ardupilot/ardupilot` and initialize submodules; pin exact upstream SHA.
2. Branch separately from upstream (e.g. `feature/hobbywing-v5-pixhawk6c-upstream`). Port only isolated `Protocol.h`, `Stream.h`, `Timing.h`, host tests and reviewed design; do not blindly cherry-pick legacy `AP_BLHeli` changes.
3. Identify exact `Pixhawk6C` board target and confirm IO/UART port mapping and current serial API on this upstream revision.
4. Implement a native, single-owner UART reader on current code; expose an explicit parameter/protocol selection, default disabled; no UART writes.
5. Test C++/Python host code and SITL before any hardware tests.
6. Build using `./waf configure --board Pixhawk6C` and `./waf copter` from the pinned upstream-based branch; record compiler/toolchain version, binary hash, firmware size and board ID.
7. Review motor-output paths, UART permissions and failsafe impacts before flashing. Use motor disconnected for first serial RX bench tests.
8. When approved, capture 32-byte Hobbywing PL5 frames (type 0x5C30, CRC16/MODBUS), count valid/invalid frames and timing, compare bench voltage.
9. Only after protocol and RPM scale validation consider flight-facing RPM publication. Keep existing Align RPM sensor independently active in Blade 500X.

## What has NOT been accomplished

No firmware build, Pixhawk6C test, flashing or live ESC reception has been verified. There is no GitHub CI result confirmed for the experimental firmware integration.

## Device selection to be recorded at test start

- FC: Holybro Pixhawk 6C stock board **or** a named custom derivative (must name exact hwdef).
- ESC: Platinum 80A V5 (current Blade 500X research) **or** Platinum 150A V5.1 (later Viper research). Do not confuse protocols without real evidence.
- UART: choose a free port and validate `SERIALx` mapping against official Pixhawk6C pinout; TELEM1 on Pixhawk4 does not automatically establish the mapping of another board.
