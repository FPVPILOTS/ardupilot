# Hobbywing Platinum V5 / V5.1 native ESC telemetry — upstream-based prototype

**Status: EXPERIMENTAL BENCH-ONLY, NOT FLIGHT-READY.** This is an independent branch originating at ArduPilot upstream master commit `8a1dfc1dbdab55f18636feb4784bea4861e2145d`, on 2026-11-19. The first target is **Holybro Pixhawk 6C**, not the legacy Pixhawk 4 fork.

Scope: Initial target is the E-flite Viper 90mm EDF fixed-wing aircraft with a Holybro Pixhawk 6C and Hobbywing Platinum **150A V5.1** ESC. Compatibility with the Platinum 80A V5 on the separate Blade 500X helicopter requires separate evidence. **No DataLink V2. No ESC configuration, UART TX, reset, motor arming bypass, throttle, or governor control.**

## Initial working hypothesis

Pixhawk 6C TELEM1 UART RX (UART7 RX / PE7 on firmware hardware definition) can accept Hobbywing V5 frames via its corresponding SERIAL1 port. **Connector pin numbers and actual electrical voltage/waveform must be checked on the bench before connecting.** Firmware SERIAL_ORDER begins `OTG1 UART7 UART5`. The Pixhawk 6C is present in this upstream base at `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C/hwdef.dat`.

## Current changes

- Portable, native C++ parser: [Protocol.h](Protocol.h). 32-byte telemetry frame, 0x5C30 frame type, CRC16/MODBUS, RTC-independent typed output.
- Streaming receiver: [Stream.h](Stream.h). Byte-wise buffering, CRC reject and resynchronization.
- Timing telemetry: [Timing.h](Timing.h). Receive intervals, EMA estimated Hz, age and maximum gaps.
- `AP_BLHeli` includes experimental **`SERVO_BLH_HWV5`** setting, default 0. At 1, the ESC telemetry SERIALx=16 UART is receive-only. In this mode it bypasses existing BLHeli 10-byte parsing and telemetry requests.
- SerialManager's ESC telemetry RX buffer increased to 256 bytes.
- Each validated V5 frame updates AP_ESC_Telem voltage/current/ESC temperature if an ESC motor output mapping exists. No fake consumed mAh. RPM is not published until scale is experimentally verified. BEC/fault fields are decoded but are not yet accessible in standard ESC telemetry.
- `HWV5 RX ok` and `HWV5 RX Hz10` debug messages appear every 5 seconds when enabled on a connected GCS. **These do not limit the actual frame-processing rate.**
- Host Python/C++ reference tests, runner and GitHub Actions workflow. CI and cross-compilation results must be verified independently.

## Host testing

From this repository root:

```bash
bash Tools/hobbywing_v5/run_host_tests.sh
```

No ESC, Pixhawk or physical motor required. This tests synthetic frames and a known CRC test vector, **not physical ESC behavior**.

## Pixhawk 6C build (operator/CI to execute; not yet evidenced)

```bash
git switch feature/hobbywing-v5-pixhawk6c-upstream-20261119
git submodule update --init --recursive
bash Tools/hobbywing_v5/run_host_tests.sh
./waf configure --board Pixhawk6C
./waf copter
```

Record toolchain, commit, firmware binary hash, board ID, flash/RAM, warnings and build result. For this aircraft the build command MUST be `./waf plane`, never `./waf copter`. Do **not** flash based only on this README.

## Bench parameter intent (custom build only)

```text
SERIAL1_PROTOCOL = 16  # Existing ESC telemetry UART
SERIAL1_BAUD     = 115 # 115200 baud
SERVO_BLH_HWV5   = 1   # New custom parameter; default off
```

Reboot after changing selection. Ensure TELEM1 is not needed for a MAVLink radio. Stock stable firmware **does not have `SERVO_BLH_HWV5`**.

## Next gates

1. CI host tests and actual Pixhawk6C ArduPlane compile.
2. Review motor-independent UART startup and no-write guarantee.
3. Scope/oscilloscope validate P signal and GND to UART RX; no ESC positive programming supply.
4. First hardware trace, startup sequencing, valid frame counter and latency.
5. Verify reported measurements against instruments, physical pole pairs / independent rotor tachometer.
6. Add BEC monitoring/fault fields via approved log/MAVLink interfaces.
7. Code review, regressions and upstream ArduPilot PR. Stable release inclusion is the maintainers' decision.

See [UPSTREAM_ACCEPTANCE.md](UPSTREAM_ACCEPTANCE.md) and [BENCH_PROCEDURE.md](BENCH_PROCEDURE.md).

## Open-source provenance

Protocol structure and decoding derived from Rotorflight: [esc_sensor.c](https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/main/sensors/esc_sensor.c), and [PL5 simulator](https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/utils/sitl/esc/pl5_esc.py). Keep all applicable GPL license conditions and attribution when preparing upstream contributions.

## First hardware diagnostic messages

With the experimental firmware **successfully built and validated**, the receive-only listener additionally reports (every 5 seconds, only after one valid frame):

```text
HWV5 RPMraw=<number> THR=<percent> FAULT=<raw byte>
HWV5 BEC V10=<decivolts> I10=<deciamps> T=<degC> MT=<degC>
```

These fields are *raw decoded observations*, not approved control inputs. The four 5-second messages do not throttle UART receive. `RPMraw` is the unconverted decoded speed field; never use as mechanical rotor or governor RPM without calibration. BEC values may be placeholders on some ESC variants. The ESC remains in receive-only mode and the existing Align rotor-RPM sensor is not modified.

## Automated build gate

A dedicated workflow `.github/workflows/hobbywing-v5-pixhawk6c-build.yml` was added to compile ArduPlane for `Pixhawk6C` using ArduPilot's ChibiOS development container, with checksum/artifact generation **only after a successful run**. GitHub Actions execution/availability and the resulting binary have not been confirmed. Do not describe the firmware as successfully built or flight-safe based on workflow presence.
