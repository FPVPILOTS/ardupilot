# Phase 15 — manually authorized functional ESC handshake

2026-11-05. Target: **Pixhawk6C-SpktSmart-HSLab**, Plane, board ID 56.

**IMPLEMENTED / TESTED / BUILD VERIFIED. Electrical qualification: UNVERIFIED. Physical handshake: NOT TESTED by this development session. Motor control and flight: NO-GO.**

This separate firmware permits the user's explicitly requested, supervised functional experiment without an oscilloscope. It does **not** turn the earlier static GPIO/voltage observations into electrical qualification. A defective or incompatible signal path could still cause electrical damage. No additional multimeter tests are a software prerequisite. Nothing has been flashed, powered, or transmitted to physical hardware by this work.

## Scope and firmware identity

Use only the new HSLab APJ identified in the build evidence below. The previous `Pixhawk6C-SpktSmart-Lab` is a disconnected waveform generator and is **not** this handshake build. The ordinary `Pixhawk6C-SpktSmart` remains protected; it has no `SPKT_LABACK` parameter and cannot physically transmit a handshake, waveform, throttle, or reverse command.

HSLab starts disarmed, rejects arming even with checks disabled or a forced-arm request, and latches the existing motor-function emergency stop. These protections remain active when `SPKT_ENABLE=0`. It has no flight capability. Other servo outputs can still follow their ordinary disarmed configuration: disconnect all actuators. MAIN3's emergency-stop command is **not** a claim that the connector is high impedance. Do not connect the ESC signal to MAIN3 or any other output during this experiment.

The stock `iofirmware_f103_lowpolh.bin` remains bundled. No experimental IO image installation is required; no new IOMCU motor-ownership experiment is part of this phase. The existing throttle function 70 → virtual CH1 and authorized reverse → virtual CH7 code is retained but cannot generate physical commands in HSLab.

## What changed

Paths below are relative to the ArduPilot repository; all previous work is retained on `feature/pixhawk6c-spktsmart`. Source revision plus the recorded dirty-file hashes, not a fictitious new commit, identify the build.

- `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C-SpktSmart-HSLab/hwdef.dat`: separate board target and short board identity. `HANDSHAKE_LAB=1`, while `HARDWARE_TX_VALIDATED=0`, `MOTOR_TX_VALIDATED=0`, and waveform `LAB_ONLY=0`.
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Lab.h`: mutually exclusive LAB gates; compile-time rejection of a handshake LAB claiming motor/electrical qualification.
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_LabHandshake.h`: explicit acknowledgement, 10-second trigger window, one native lease per boot, maximum two correctly addressed handshake frames, 200-ms limit.
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Diagnostics.{h,cpp}`: LABACK at new parameter index 9; startup reset, unconditional LAB arming veto, abort, explicit results and rate-limited physical-transport counters. Existing parameter indices are preserved.
- `libraries/AP_HAL/UARTDriver.h`: optional dedicated handshake-start operation, false by default.
- `libraries/AP_HAL_ChibiOS/DiagnosticRX.{h,cpp}`: native preflight checks and independent transaction authorization; RX restoration and observable LAB flag without asserting qualification.
- `libraries/AP_HAL_ChibiOS/SpektrumUART4.{h,cpp}`: reuse actual TC interrupt, FIFO, ownership, watchdog and open-drain UART path; enforce LAB packet whitelist and native frame/time limits before enabling TE.
- `libraries/AP_SpektrumSmartESC/tests/test_spektrum_{hslab,lab,bench}.cpp` and `libraries/AP_HAL_ChibiOS/tests/{test_spektrum_uart4_registers.cpp,test_diagnostic_rx_registers.cpp,SpektrumUART4_test_hal.h,DiagnosticRX_test_hal.h}`: focused authorization, actual-method register-model, frontend, abort and arming tests.
- Workspace `tools/{validate-spktsmart-phase15.sh,verify_spktsmart_phase15.py,test_spktsmart_phase15_sitl.py}`: reproducible isolated validation, source/artifact checks, and real Plane SITL arming/startup tests.

No new parser or parallel bus master was introduced. The existing `AP_SpektrumSmartESC_Reader` constructs and validates the handshake. The previous native-SRXL2 integration, CRC, channel-isolation and control mapping work is preserved.

## Reference comparison and bounded sequence

References: [official Spektrum SRXL2](https://github.com/SpektrumRC/SRXL2), Rev K specification §§7.1.1, 7.2, 7.2.1, and [pinned WingFlight driver](https://github.com/WingFlight/wingflight-firmware/blob/da67c91c31f3a87c6e13e6bc8aeb49ce59eef138/src/main/drivers/srxl2_esc.c). The file-history API checked on 2026-11-05 still identifies that WingFlight revision. Local reference inputs and original license notices are preserved in `sources/phase15-wingflight`; they are not linked into the firmware.

| Aspect | WingFlight reference | This bounded experiment |
|---|---|---|
| Identity | Default FC `0x30`, priority 10, UID `0x4156` | Existing FC `0x31`, priority 20, hardware-ID-derived UID |
| Discovery | Startup retries, alternating destination `0x40`/`0x00` | One directed `0x40` query after a 50-ms observation period |
| Response/finalization | Addressed response then final broadcast, possible baud change | Accept actual `0x40 → 0x31` reply, then `0x31 → 0xFF` baud-selection handshake |
| Baud | 115200 initially; supports switching | 115200 throughout; baud selection byte 0 |
| Turnaround | Reference includes software completion/echo mechanisms | Real UART TC interrupt; no byte-count suppression of replies |
| Electrical drive | Reference configuration is not transplanted | PA0 AF8, UART4 HDSEL, open drain, no internal pull-up |
| Motor/telemetry traffic | Normal operation includes Control Data | All Control Data `0xCD`, reverse and motor packets rejected |

`0x00` is the destination for a slave's startup announcement, not the final baud-selection broadcast. `0xFF` is that final broadcast. Device type 3 identifies an FC; **neither `0x30` nor `0x31` proves that this Avian accepts an FC as its master**. Priority is a telemetry request preference, not a master-election credential. The documented dedicated-UART master may poll a specific device; receiverless Avian interoperability is precisely what this experiment tests. The reference is not a physical capture or evidence of this ESC revision's behavior.

Only packets `A6 21 0E ... CRC16` are permitted. The first goes to `SPKT_HSID` (64/`0x40` by default); the second goes to `0xFF` only after the reader validates a timely reply from that target to `0x31`. Both carry the same local UID. A received packet with the local source/UID is counted as echo and cannot satisfy the response requirement. Wrong IDs, wrong destination, unexpected packet types or UART faults terminate the experiment. CRC failures are counted and cannot count as a reply.

The response window is 50 ms from actual query TC. The entire native authorization is less than 200 ms; no new TX can start at or after 197 ms. Each TX has a 3-ms interrupt-driven watchdog. At 115200 baud, a 14-byte 8N1 packet nominally occupies about 1.22 ms; this is calculated timing, **not a measured connector waveform**. Final TX waits at least 2 ms since the last acquired RX byte. No repeated query, automatic scan, startup TX, periodic keepalive, or retry loop exists. After success the link may naturally go idle/reset because this build intentionally sends no control/telemetry polling frames.

## Firmware checks and their limits

Before the native lease can start: dedicated LAB identity, fresh manual acknowledgement, disarmed state, arming inhibition, emergency stop, `ENABLE=1`, `PROBE=0`, available UART, high input, empty RX buffer, no UART errors, no competing owner, PA0 AF8/open drain, UE+RE set, TE+TCIE clear, HDSEL set and SWAP clear. BUSY or pending hardware RX prevents acquisition. These checks are repeated where appropriate before TX.

The native FIFO buffer remains owned until TC or abort. TC detaches the pin to input, disables TE, restores RE and the open-drain AF, then records completion. RX data that arrives after TC is preserved for the main-task reader. The 1-kHz native service also enforces lease expiry independently of the vehicle task. Logs report UART state after restoration.

Internal status cannot measure connector overshoot, rise time, unknown board-buffer direction, external high impedance or electrical contention during TX. There is no claim that collision detection covers all electrical collisions. No scope/logic-analyzer waveform evidence exists. The user's earlier ~3.0-V ESC idle and GPIO pull tests support an RX path but do not establish these dynamic properties.

## Exact Mission Planner procedure

This is one supervised experiment, not aircraft setup. Do not load an old parameter file containing nonzero probe/waveform commands.

1. Remove ESC battery and USB power. Disconnect and individually insulate **all three motor leads**; remove any propeller, EDF rotor or fan. Disconnect all other actuators. Disconnect the ESC signal from MAIN3 and all outputs. Disconnect and individually insulate the ESC BEC **positive/red** wire. The 6-V BEC must never connect to AUX7 signal or USB power.
2. Keep the ESC entirely disconnected from Pixhawk while flashing. Back up current Mission Planner parameters and save the known-good ordinary APJ. Manually use *Install Firmware → Load custom firmware* for the HSLab APJ below, with the Pixhawk USB-powered. Do not install an IO image separately. Nothing automatically triggers on flashing or reboot.
3. Set the following, write parameters, then reboot with ESC still disconnected:

   | Parameter | Value | Purpose |
   |---|---:|---|
   | `SPKT_ENABLE` | 1 | Dedicated AUX7 115200 RX |
   | `SPKT_PROBE` | 0 | No GPIO pull experiment |
   | `SPKT_HS` | 0 | No request |
   | `SPKT_LABACK` | 0 | No acknowledgement |
   | `SPKT_HSID` | 64 | Candidate ESC ID `0x40` |
   | `SPKT_WAVE` | 0 | No waveform request |
   | `SPKT_TLM` | 0 | No telemetry publication for this handshake experiment |
   | `SPKT_SHADOW` | 0 | No preview needed |
   | `SPKT_EXP` | 15 | Log experiment label |
   | `LOG_DISARMED` | 1 | Record evidence while disarmed |

   Keep aircraft arming/safety configuration intact. Do not set `ARMING_REQUIRE=0` or disable checks; those values were only used to challenge the software in SITL. Do not change any RC receiver `SERIALx_*` settings. AUX7 is not a SerialManager port.

4. Verify the exact APJ hash and periodic `SPK15 HSLAB` identity, `U1`, `RX1`, `ACK0`, `USED0`, zero TX, and disarmed status. A disconnected floating line need not read high; no request is allowed yet. Confirm no startup TX was reported. This is MCU evidence, not an external waveform test.
5. **Acknowledge that connector-level TX behavior remains electrically unverified before proceeding.** With both sides unpowered, connect ESC signal to FMU PWM OUT **pin 8 / AUX7**, and common signal ground to **pin 10 / GND**. Check the connector orientation against [Holybro's controller-side pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports); the mating cable view is mirrored. BEC positive stays isolated. Do not connect another receiver, output or signal source. No extra flight interface is introduced.
6. Power Pixhawk by USB, then power the ESC from its appropriate 3S–6S battery. Motor leads and actuators remain disconnected. Wait for diagnostics. Require `U1`, `RX1`, line `L1`, `FE0`, zero other UART errors, disarmed status and zero TX. If these checks fail, stop; do not bypass the refusal or apply a test signal. The connected ESC provides the observed idle bias; this build does not add a strong pull-up or assume that static voltage proves adequate rise time.
7. In the Full Parameter Tree/List, write **`SPKT_LABACK=15`**. This explicitly acknowledges the remaining electrical uncertainty and the disconnected motor/actuator conditions. Expect `ACK accepted; HS=1 within 10s`. Within ten seconds write **`SPKT_HS=1`**. Both are consumed in RAM. Do not write them repeatedly or combine them into a startup parameter file. Only one accepted experiment is permitted per reboot, including a native-preflight refusal after acknowledgement was consumed.
8. Watch Messages and retain the Mission Planner telemetry log. Expect `WAITING; one attempt`, then a result. Allow five seconds for the rate-limited counters. `PASS` requires one matching CRC-valid **received peer** handshake and final handshake TC/RX restoration; `TX1` alone, `CRCOK1` alone, or echo is not success. No motor command is sent regardless of outcome.
9. To abort: write **`SPKT_HS=-1`** or **`SPKT_ENABLE=0`**. A parameter command may arrive after the already bounded experiment finishes; it is not a sub-millisecond hardware kill. **Immediate physical shutdown is removal of ESC battery power.** The onboard TX watchdog and total lease provide the short time bounds. Keep the motor disconnected throughout.
10. After the result, remove ESC battery, retain USB briefly to save logs, then remove USB before rewiring. Export DataFlash `.BIN` and Mission Planner `.tlog`; retain the firmware SHA, target ID, result and raw received bytes. Do not reboot into repeated attempts to hunt for a response. Review a timeout/error before another manually authorized experiment. Restore ordinary firmware and backed-up parameters with ESC/actuators disconnected before leaving bench work; set `SPKT_ENABLE/PROBE/HS/WAVE/SHADOW=0`, reboot and verify the ordinary build identity. Never fly HSLab.

## Diagnostic meanings

| Message / log | Meaning |
|---|---|
| `SPK15 HSLAB U1 RX1 ACK0 USED0` | LAB identity; UART initialized; MCU RX-only configuration; pending acknowledgement; once-per-boot token consumed |
| `SPK15 TXn RXBn CRCOKn IDxx` | TC-completed handshake frames; captured physical receive bytes during the attempt; CRC-valid received frames (may include echoes); accepted peer ID in hex |
| `SPK15 TYPExx ECHOn TMOn BUSn` | Last CRC-valid packet type; local-source/UID echoes; reader timeouts; native handshake fault count |
| `SPKT PEERn ECHOn CRCERRn` | Matching requested peer responses, echoes and failed CRCs; a real result requires `PEER1` |
| `SPKT U1 L1 C0 FE0 Txx Sxx` | Passive UART initialized, MCU input high, parser CRC failures, cumulative framing errors, last packet/sensor type |
| `SPKT UART FE... ERR...` | UART framing and total receive error counts; nonzero errors are not an enable flag |
| `SPK15: PASS RX_RESTORED=1; AUTH expired` | Matching peer reply plus final TX completion and observed RX restoration. Not a telemetry, throttle, reverse or electrical qualification result |
| `TIMEOUT ...` | No accepted timely reply / sequence deadline expired; does not distinguish wrong target, ESC initialization requirements, electrical path or timing incompatibility |
| `ERROR ...` / `RX_RESTORED=0` | Unexpected packet, native UART/configuration/turnaround fault or abort; power down and review |
| `ACK BLOCKED` / `SPKT HS: BLOCKED; no TX` | Missing/expired/used acknowledgement, unavailable/inconsistent UART or unmet prerequisites; no authorized experiment |
| `SPKM`, `SPKE`, `SPKX`, `SPKW`, `SPKH` | Counts/state; matching peer/stale bytes; first 64 actual RX bytes as hex; TC/flags/priority/baud/UID; raw UART/error status |

All byte/frame counts in the HSLab firmware come from the native UART/reader, not the mock tests. TX count increases only on reported TC; an aborted partial frame may have emitted bytes while `TX` remains zero. `RX_RESTORED` describes registers/pin configuration, not measured connector impedance. `SPKW.Gate` stays **0** (unqualified); its flag bit 4 identifies experimental HSLab permission. Logging timestamps are software times, not a wire capture.

## Validation and artifacts

The flashable handshake-only artifact is workspace **`custom-firmware/Pixhawk6C-SpktSmart-phase15-hslab/arduplane.apj`**.

SHA-256:

```text
7ab1c3f6355c08fbd5003cbc98f702a7a134c7b521ada37211baf92b7a410778
```

Its `.bin`, `.elf`, `build-manifest.json` and `SHA256SUMS` are alongside it. Ordinary regression images are in `custom-firmware/Pixhawk6C-SpktSmart-phase15-regression/`. Those ordinary images cannot perform this experiment.

| Build | Result | Flash used, waf | Static data+BSS | Compiler warnings | APJ SHA-256 |
|---|---|---:|---:|---:|---|
| HSLab Plane | PASS | 1,600,376 B | 146,468 B | 0 | `7ab1c3f6355c08fbd5003cbc98f702a7a134c7b521ada37211baf92b7a410778` |
| Protected normal Plane | PASS | 1,598,176 B | 146,428 B | 0 | `402cb6bcd0358ba5d5933c4490ed5b1157af1ac2a32f683c3de1cb51a4cb263b` |
| Protected normal Copter | PASS | 1,601,932 B | 147,520 B | 0 | `68965d2abf1db2c52297b772eb1f2d2bfec3c8c812cdb6ea057273baa4efe1c7` |

The LAB APJ image payload is 1,600,384 bytes including binary padding. Static RAM is not a peak stack/heap measurement. All images identify board 56 and base source `dbe792162d06cab66c3475fd5556bf7a120f119e`; the manifest records 95 LF-normalized project-file hashes, 17 changed/new Phase 15 files, exact tools and artifacts. No new commit was created. Current host sources and tools were checked against that manifest after copying the artifacts.

Build environment: `ardupilot/ardupilot-dev-chibios:v0.1.3`, digest `sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`, Arm GCC 10.2.1, existing `/build/src` snapshot and named volume. Waf targets:

```text
./waf configure --board Pixhawk6C-SpktSmart-HSLab --out /build/phase15-hslab --no-submodule-update
./waf plane -j6
./waf configure --board Pixhawk6C-SpktSmart --out /build/firmware --no-submodule-update
./waf plane -j6
./waf copter -j6
```

| Focused validation executed | Actual result |
|---|---|
| GTests: authorization/lease, handshake reader, real diagnostic frontend with mock port, real arming entry points | **23 passed** (6 + 12 + 4 + 1) |
| ASan/UBSan: policy, ordinary UART methods, HSLab UART methods, native preflight | **24 passed** (6 + 9 + 7 + 2), no sanitizer errors |
| Actual Plane SITL with HSLab configuration and no UART | PASS: saved requests ignored; forced arming denied with checks disabled; missing UART blocks acknowledgement/TX; abort consumed |
| Compiled ELF/APJ audit | PASS: board ID, APJ/BIN/ELF payload identity, correct LAB flags, ordinary TX methods reduce to false-return stubs, HSLab waveform disabled, no active Motor/ChannelData/IOOwner backend linked |
| Bundled IO | PASS: stock image SHA `38c89aec93f3acd69c9f6065347e2090615e4f1e87bc72eefdd3dca347188f1e`; no experimental IO replacement |
| Generated LAB hwdef inspection | PA0 boots INPUT with AF0, reserved from PWM; dedicated UART4 backend; PA1 remains TIM5_CH2/PWM8. Existing compile-time checks reject competing SERIAL/SIO/UART4 driver ownership |

The integration test's external reply is explicitly mocked; SITL has no physical UART and cannot produce `PASS`. Neither constitutes the physical success milestone. Completed GPIO, general CRC/channel-mapping and broad simulation suites were not repeated.

Logs: workspace `logs/spktsmart-phase15-linux/`, including `verification-summary.json`, `artifact-audit.log`, complete waf logs, individual test logs and `sitl-lab/results.json`. The reproducible workflow is `tools/validate-spktsmart-phase15.sh`. No physical device was exposed to the container. An initial build rejected the long board display name; `CHIBIOS_SHORT_BOARD_NAME` was corrected and the final build passed. The full build log also retains a non-fatal Git metadata query failure in the copied build environment; the APJ base Git identity and explicit source hashes were verified independently.

An optional additional Docker audit was not executed because automatic approval review reached a usage limit. It did not invalidate the earlier successful binary/stock-IO audit or the subsequent local source/artifact verification; no claim is made that this extra check ran.

## Decision and next evidence

The supervised handshake-only experiment is the next manual step once the operator accepts the explicitly stated electrical uncertainty. **Electrical qualification remains UNVERIFIED.** A successful handshake would establish functional protocol exchange for this setup; it would not establish electrical margin, robust bidirectional operation, telemetry polling compatibility, motor control, direction-change behavior, or IOMCU output arbitration.

Required next evidence is the actual raw response and CRC, source/destination IDs, TX/peer/echo counters, timeout/error status, RX restoration and firmware identity. Silence is an honest result, not proof of a broken ESC. This build never sends the Control Data polling that may be required for telemetry; telemetry and motor experiments require separate review. Do not enable throttle, reverse, a motor test, or flight based on this handshake result.
