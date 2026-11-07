# Pixhawk6C-SpktSmart

Experimental Spektrum Smart ESC development target for Holybro Pixhawk 6C.
This is the current project hardware target, replacing Pixhawk 4 following the
user's correction. Branch: `feature/pixhawk6c-spktsmart`. Source revision:
`dbe792162d06cab66c3475fd5556bf7a120f119e`. AI-assisted implementation.

**No physical Smart ESC control is enabled. Not flight-ready.**

Phase 3 implements an opt-in native UART4 receive-only backend and shared vehicle
diagnostics. Both `SPKT_ENABLE` and `SPKT_TLM` default to zero. No transmitter,
throttle or reverse path exists. See [PHASE3.md](PHASE3.md) for exact build/test
evidence, configuration, acquisition limits and the physical validation boundary.
The [Phase 2 report](PHASE2.md) is retained as historical evidence.

## Vehicle scope

The protocol implementation lives in `libraries/AP_SpektrumSmartESC`, independent
of vehicle classes, HAL peripherals and board pins. The board target is usable
with the normal ArduPilot vehicle build commands; it is not an ArduPlane-only
target. Plane, Copter and other supported vehicle types need their own final
output, arming, failsafe and reverse-thrust integration/validation. Copter motor
mixing must not inherit Plane reverse-thrust assumptions. Availability of a
build command does not establish ESC motor-control compatibility.

```sh
./waf configure --board Pixhawk6C-SpktSmart
./waf plane       # build/Pixhawk6C-SpktSmart/bin/arduplane.apj
./waf copter      # build/Pixhawk6C-SpktSmart/bin/arducopter.apj
./waf rover       # build/Pixhawk6C-SpktSmart/bin/ardurover.apj
```

The build commands above describe the standard workflow. Plane and Copter APJs
have been produced in the workspace
`custom-firmware/Pixhawk6C-SpktSmart-phase3-artifacts/`. No hardware was flashed
or tested. Other vehicle builds, including Rover, have not been verified.

## Board-specific audit

Sources: `../Pixhawk6C/hwdef.dat`, `../Pixhawk6C-bdshot/hwdef.dat`,
`../Pixhawk6C/hwdef-bl.dat`, `../scripts/STM32H743xx.py`, and
`AP_HAL_ChibiOS/UARTDriver.cpp`. These replace the Pixhawk4/F767-specific findings.

| Resource | Pixhawk 6C mapping | Finding |
| --- | --- | --- |
| MCU | STM32H743xx | H7 target, not the Pixhawk4 F7 hardware. |
| AUX1 | PA8, TIM1_CH1, GPIO50 | UART7_RX AF11 exists; USART1_CK is a clock, not UART TX. No direct UART TX on this pin. UART7 is already TELEM1. SWAP/half-duplex interactions and shared-port ownership would need separate proof. |
| AUX2–4 | PE11/PE13/PE14, TIM1_CH2–4 | Share TIM1 with AUX1. No UART data alternate function found in the local pin table. |
| AUX5–6 | PD14/PD15, TIM4_CH3–4 | UART8 CTS/RTS are flow-control lines, not TX/RX. UART8 already serves GPS2. |
| AUX7 | PA0, stock TIM5_CH1 / GPIO56 | UART4_TX AF8 supports one-wire RX. PA0 is input at boot and is no longer exported as GPIO56. The dedicated RX backend can select AF8 with TE clear after explicit enable. It is not a SERIALn port. Electrical routing and timing remain unverified. |
| AUX8 | PA1, TIM5_CH2, GPIO57 | UART4_RX AF8 exists; shares TIM5 with AUX7. Check timer/DMA grouping before allocating UART4. |
| TELEM1 | UART7, PE8 TX / PE7 RX | Existing UART candidate, with port ownership and electrical verification required. |
| TELEM2 | UART5, PC12 TX / PD2 RX | Existing UART candidate, with port ownership and electrical verification required. |
| Bootloader | Includes UART7 and UART5 | Both telemetry ports appear in the stock bootloader. Boot-time behavior must be included in the electrical/motor-safety audit. |
| Bidirectional DShot | BIDIR on PA8, PE13, PD14, PA0; different DMA priority | Does not prove SRXL2 electrical or serial compatibility. |

Do not transfer the earlier statement that Pixhawk4 AUX1 has no UART alternate
function to this board. On 6C it has an RX function; AUX7 additionally has a TX
function. Neither is proof that the connector can safely exchange Smart traffic.
Schematic/buffer direction, voltage tolerance, idle drive, contention, turnaround
and deterministic timing remain unverified. No connection should be driven yet.

The variant inherits the stock Pixhawk6C board ID (56), bootloader, USB IDs,
IOMCU firmware, peripherals and battery-monitor defaults. AUX1 is reserved as
an input, and now also reserves AUX7 as an input. AUX2–6 and AUX8 retain their
existing numbering and normal behavior; AUX7 has not been remapped to UART4.
GPIO50 remains accessible to explicitly configured GPIO users and must not be
assigned during experiments. GPIO56 is removed from the custom target to protect
the dedicated AUX7 receiver from unrelated GPIO users.

The first validation script initially assumed Pixhawk4 timer-group ordering;
the generated 6C definitions revealed TIM5/TIM4/TIM1 ordering. The verifier now
checks the correct groups for each board. No output remapping was needed.

## Software and validation status, 2026-11-01

| Area | Status |
| --- | --- |
| Shared passive protocol | IMPLEMENTED: framing, CRC, handshake observation, packet serialization, channel/failsafe decoding and 0x20 telemetry decoding. |
| Host tests | TESTED: 64 ArduPilot GTests and 56 standalone ASan/UBSan cases pass, including mock bidirectional session and command tests. |
| Firmware compilation | BUILD VERIFIED: Linux builds for custom and standard Pixhawk6C, both Plane and Copter. |
| 6C generated definitions | TESTED: six checks pass; six additional checks pass for the uninstalled AUX7 UART4 candidate. |
| Build environment | Official ArduPilot CI image on Docker Desktop/WSL2; evidence in `logs/spktsmart-phase5-linux/`. |
| Diagnostic transport | Native receive-only backend integrated, disabled by default; physical reception remains unverified. |
| Telemetry publication | Connected through the shared receive service with acquisition timestamps, poll correlation and stale withdrawal. Opt-in, fixed telemetry slot 15. |
| Bidirectional transport | Mock session tested; native UART4 port prepared and target-compiled but uninstantiated. Hardware TX gate remains zero; circuit and ESC initialization are unverified. |
| Throttle, reverse, vehicle safety integration | Simulation-only calibrated command tests; no physical commands or vehicle motor integration. |
| Physical ESC communication and motor response | Not HARDWARE VERIFIED. No firmware flashed. |

Reproduce the independent tests from the source root:

```sh
python libraries/AP_SpektrumSmartESC/tests/run_host_tests.py --arm-cxx arm-none-eabi-g++
python libraries/AP_SpektrumSmartESC/tests/verify_hwdef.py --board Pixhawk6C
python libraries/AP_SpektrumSmartESC/tests/verify_aux7_candidate.py
```

Logs and generated definitions reside in the workspace `logs` directory and
source `build` directory. The earlier [Pixhawk4 audit](../Pixhawk4-SpktSmart/README.md)
is retained as historical protocol research and lists protocol limitations and
the required hardware/failsafe validation scenarios. Its pin, bootloader,
parameter-default and build-target conclusions do not apply to Pixhawk6C.

Next tasks: verify the PA0/AUX7 board signal path, idle impedance, native reception
and FIFO latency, then validate any future transmitter and per-vehicle motor
safety integration. Physical transmission remains unavailable until the
electrical and target protocol assumptions are validated. The user's PCB revision
is unknown; no deliberate signal-voltage modification is known.

See [PHASE4.md](PHASE4.md) for exact changes, test/build evidence, state-machine
behavior and the required hardware validation boundary.

Phase 5 follows the official bdshot PA0 return-path evidence. See
[PHASE5_DSHOT_ANALYSIS.md](PHASE5_DSHOT_ANALYSIS.md) for the UART4 backend,
18 resource checks, final firmware evidence and GO/NO-GO/UNVERIFIED assessment.

Phase 6 adds receive-only bench arming inhibition, an emergency-stop latch,
SPKH UART/error diagnostics and an experiment label. Defaults remain disabled.
See [PHASE6_HARDWARE_VALIDATION.md](PHASE6_HARDWARE_VALIDATION.md) and
[PHASE6_MEASUREMENTS.csv](PHASE6_MEASUREMENTS.csv). Hardware remains UNVERIFIED;
no ESC or test generator may be connected before electrical review.
Phase 6 validation is focused: 15 relevant GTests and 6 bench sanitizer cases;
the unchanged Phase 5 protocol/resource matrix was not repeated.

The [AUX1–AUX8 comparison](AUX1_AUX8_SRXL2_COMPARISON.md) retains AUX7 as
the primary recommendation and TELEM3/USART2 as UART fallback. It corrects earlier
RX-only exclusions: AUX8/UART4_RX and AUX1/UART7_RX are conditional HDSEL+SWAP
candidates. Paired bdshot capture also exists for outputs without an explicit
BIDIR marker. No firmware, output allocation or electrical gate was changed.

[PHASE7_BENCH.md](PHASE7_BENCH.md) is the current AUX7 operator checklist.
Phase 7 corrects TC fault cleanup and duplicate RX-release flushing in the
uninstantiated prepared port, adds 8 sanitized register-model regressions and
rebuilds only the affected custom Plane/Copter targets. Hardware TX remains
disabled; physical approval requires actual revision/circuit and scope evidence.

[PHASE8_MAIN3_SMART_THROTTLE.md](PHASE8_MAIN3_SMART_THROTTLE.md) audits physical
MAIN3 (IOMCU PB8/TIM4_CH3), which has no hardware UART. AUX7 remains the recommended
candidate. A transport-independent simulation interface prepares virtual SRXL2
throttle channel 1 and configurable reverse channel 7, with ownership and safety
checks. Its 13 GTests and 13 sanitizer cases pass; custom Plane/Copter builds pass.
No live vehicle-demand hook, physical output ownership or motor TX is enabled.
Existing IOMCU firmware images remain unchanged; electrical validation is pending.

[PHASE9_SHADOW.md](PHASE9_SHADOW.md) adds default-off SPKT_SHADOW Plane observation
of final cached SERVO3/function70 PWM and signed demand, after output processing.
It logs SPKS at 1 Hz without packets, transport selection, PWM inhibition or
physical control. Copter has no adapter. Raw ESC channel calibration and an actual
output arbiter remain unimplemented. Shadow logging does not stop conventional PWM.

[PREFLASH_PHASE9.md](PREFLASH_PHASE9.md) records the saved-artifact safety audit,
exact SHA-256, disconnected USB bench procedure, parameter backup and rollback.
Its BENCH-FLASH GO does not authorize connecting an ESC or testing motor outputs.

[PHASE10_REAL_ESC_BENCH.md](PHASE10_REAL_ESC_BENCH.md) documents the latest passive
ESC bring-up firmware: rate-limited UART/frame summaries and historical SPKR
metadata, plus an offline one-shot handshake experiment. Direct ESC connection
and physical handshake TX remain NO-GO pending electrical evidence and approval.
Use the Phase 10 artifact manifest/checksums; earlier audits describe earlier images.

[PHASE11_AUX7_HARDWARE_PROBE.md](PHASE11_AUX7_HARDWARE_PROBE.md) provides the latest
closed-enclosure, disconnected AUX7 measurement procedure and default-off
SPKT_PROBE modes. Weak internal pulls are MCU-only experiments, not proof of an
external high-impedance or receive path. No external pull-up, ESC connection or TX
is approved. Record actual results in PHASE11_MEASUREMENTS.csv (initially NOT_RUN).

[PHASE12_REAL_SRXL2_READER.md](PHASE12_REAL_SRXL2_READER.md) adds the manual HS
request and a bounded FC-ID 0x31 handshake coordinator. The released image retains
TX gate 0 and reports BLOCKED; no polling/control data or physical success is
claimed. It also fixes DataFlash schema lengths. Use only the final Phase 12
checksums, not interim artifacts. Electrical release/contention evidence is pending.

[PHASE13_SPEKTRUM_THROTTLE_REVERSE.md](PHASE13_SPEKTRUM_THROTTLE_REVERSE.md)
documents the prepared mock master/control coordinator, verified wire codec,
explicit direction policy, abstract output-inhibit receipts and live read-only
Plane intent adapter. The Phase 13 APJs retain physical TX gate 0. Production
IOMCU arbitration and native motor commands remain unavailable; no motor-control
or flight-readiness claim is made. Use the Phase 13 manifest and SHA256SUMS for
these newer observation builds.

[PHASE14B_NATIVE_SRXL2_INTEGRATION.md](PHASE14B_NATIVE_SRXL2_INTEGRATION.md)
describes native CRC reuse, the gated final-Plane/UART4 binding and real optional
IO capability client. It adds a separate fixed-pattern LAB image for disconnected
AUX7 measurements and an unbundled MAIN3-inhibited IO development image. Normal
and lab images both retain handshake/motor qualification gates 0; only the lab
image accepts the manual waveform command. Do not flash the IO image or connect
an ESC to the lab image. Production activation/image pairing remains incomplete.
