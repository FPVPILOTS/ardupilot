# Phase 4: gated bidirectional SRXL2 preparation

2026-11-01, Europe/Amsterdam. AI-assisted implementation, continuing Phase 3 on
`feature/pixhawk6c-spktsmart`. Base commit:
`dbe792162d06cab66c3475fd5556bf7a120f119e`. ChibiOS gitlink:
`d103983f4362e5506bcf7062ccacf345fc0c0859`; waf gitlink:
`35eadbb64e2052099a853b571e507c33032b392c`.
There is no Phase 4 commit: changes remain reviewable in the working tree.
The artifact manifests record SHA-256 hashes of the actual local project sources.
Existing unrelated changes were preserved; no submodule, system firmware or BIOS
was modified. No hardware was flashed or energized.

**IMPLEMENTED / TESTED / BUILD VERIFIED:** hardware-independent bidirectional
session, simulated commands, calibrated measurement utilities, retained native
RX-only telemetry integration and four firmware builds.

**HARDWARE UNVERIFIED / BLOCKED:** direct AUX7 electrical compatibility, physical
bidirectional transport, target-specific ESC-only initialization, throttle and
reverse. These phases are not declared complete or flight-ready.

## Hardware assessment

| Item | Evidence and result |
| --- | --- |
| AUX7 pin | PA0, UART4_TX AF8; normally TIM5_CH1/PWM7. FMU PWM OUT connector signal pin 8. MCU mapping is in `../Pixhawk6C/hwdef.dat` and `../scripts/STM32H743xx.py`; connector mapping is in the [Holybro pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports). |
| MCU single-wire receive | [STM32 RM0433, USART single-wire half-duplex](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): HDSEL joins TX and RX internally on TX. UART4 can use PA0 for both directions; PA1 is unnecessary for reception in this mode. |
| MCU line release | RM0433 describes alternate-function open-drain with an external pull-up. Separate TE/RE controls permit receive operation with TE clear. A future TX backend must wait for physical TC, release the driver and enable reception before accepting a response. GPIO open-drain alone does not prove connector high impedance. |
| Existing RX backend | `DiagnosticRX.cpp` retains TE clear, never writes TDR, configures PA0 as AF8 open-drain/no-pull only after checking registers, and returns PA0 to input on shutdown. No RX-start transient through `sdStart`, which would enable TE. Firmware disassembly verifies the write stub returns zero. |
| Signal voltage | Spektrum Rev K electrical section specifies a 3.3 V idle-high signal with weak pull-up/high-impedance idle. Its bus power voltage is a separate quantity. [Holybro PWM voltage modifications](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod) vary by RC09/RC12/RC13 board revision and can select 3.3/5 V output. Output voltage support does not establish input propagation or SRXL2 suitability. |
| Pull resistor | The MCU open-drain recommendation and the bus's weak-pull-up convention require a verified actual pull-up and measured rise time. Existing native RX adds neither pull-up nor pull-down. The generated UART candidate's pull-up is a hypothetical resource test, not installed firmware or an electrical approval. No resistor value is guessed. |
| Buffer/protection/direction | User's PCB revision is unknown and no deliberate signal-voltage change is known. [Holybro downloads](https://docs.holybro.com/autopilot/pixhawk-6c/download) expose CAD rather than a revision-matched AUX buffer schematic. No verified schematic/component list establishes buffer direction, output-enable control, protection/clamping, propagation delay or connector receive path on this board. **HARDWARE UNVERIFIED**. |
| Contention | A continuously enabled output buffer could block reception or contend with the ESC even while MCU TE is clear. Software cannot establish safe connector release without the circuit and measurements. Direct ESC connection is not approved. |
| AUX8 | PA1 remains TIM5_CH2/PWM8. No UART4_RX pin is claimed. Native RX uses no DMA or UART IRQ, leaving the existing TIM5/PWM8 allocation intact. No new PWM/DShot ownership is introduced. Future UART4 DMA/IRQ changes require another resource review. |
| Timing | Current RX is 115200 8N1, with 16-byte FIFO serviced by a 1-kHz timer. Measured FIFO latency, turnaround, rise time, bus loading and collision behavior are unavailable. The mock's 174-us idle guard is two characters at 115200; its 5-ms telemetry window is a conservative diagnostic policy, not a proven Avian deadline. |

The best MCU candidate remains AUX7. AUX8 offers UART4_RX, not the TX pin needed
for HDSEL. Other AUX mappings were reviewed in Phase 2; none establishes a better
documented native one-wire path. It is not proven that AUX7 is impossible; it is
also not proven safe. No external converter is introduced.

If AUX7's circuit proves unidirectional, the next candidate is TELEM3/USART2
(PD5 TX, PA3 RX, SERIAL5 in the stock ordering), subject to connector-level
electrical verification, port ownership and actual board revision. Some TELEM3
extra pins have revision-dependent I2C functions; they must not be commandeered.
That fallback is not implemented or claimed electrically compatible here.

## Exact code changes

Under `libraries/AP_SpektrumSmartESC/`:

- `AP_SpektrumSmartESC_Bus.{h,cpp}`: independent session and abstract exclusive
  port contract, owned TX buffer, TC-based release, idle/collision checks,
  bounded discovery retries, reply validation, peer UID change detection,
  explicitly requested mock polling, freshness and timeout counters.
- `AP_SpektrumSmartESC_Commands.{h,cpp}`: simulation-only calibrated channel-data
  construction and fail-closed authorization/direction state. No vehicle hook.
- `AP_SpektrumSmartESC_Measurements.{h,cpp}`: optional calibrated electrical-to-
  mechanical RPM and continuous-current capacity calculations. No vehicle
  calibration or runtime publication is enabled by these helpers.
- `tests/test_spektrum_phase4.cpp`: 20 independent deterministic mock tests,
  usable as a GTest binary and standalone sanitizer binary.
- `tests/verify_hwdef.py`: existing sixth check additionally requires hardware
  TX validation macro to be zero.

The custom `hwdef.dat` explicitly defines
`AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED 0`. There is no user parameter to
override it. Standard Pixhawk6C definitions are untouched. AUX1 reservation from
earlier phases remains; this phase does not restore or reassign that output.

Workspace tools add `validate-spktsmart-phase4.sh` and
`inspect_spktsmart_phase4.py`; `collect_spktsmart_artifacts.py` now accepts phase 4.
The target README links this report. Earlier code, test results, logs and artifacts
remain available.

## State machine and transport boundary

| State | Behavior |
| --- | --- |
| DISABLED | No port lease or TX. Explicit start required. |
| PASSIVE_RX | Port released to receive. Entered with any unverified gate or when yielding to another master/unexpected device. No automatic promotion. |
| BUS_IDLE | Mock waits for discovery listen interval and idle-high guard. |
| HANDSHAKE_PENDING | Own immutable packet buffer while awaiting idle guard; bounded pending timeout. Also used for an explicitly requested mock telemetry poll. |
| TX_ACTIVE | Port owns the buffer until physical Transmission Complete. Collision or stalled TX faults and requests immediate receive release. |
| WAITING_FOR_RESPONSE | Receive release has completed. CRC, IDs, response window and packet structure must match the outstanding query/poll. |
| CONNECTED | Mock discovery completed; telemetry still requires an explicit validated poll experiment. No automatic motor command or poll. |
| COMMUNICATION_LOST | Data withdrawn, no TX/retry loop. Explicit restart and rediscovery required. |
| FAULT | Collision, failed release, stuck bus/TX or changed peer UID. No automatic recovery. Failed release retains ownership rather than falsely freeing the port. |

The port contract requires exclusive acquisition without TX, an idle/collision
indication, asynchronous buffer ownership through physical TC and release that
aborts any transmission. DMA-empty is not TC. The session accepts packets only
after releasing the line. Query retries are capped at three; telemetry expires
at 50 ms and a poll response must arrive within 5 ms. Unsigned elapsed-time tests
cover timer wrap. Another master causes yielding; CRC failures and unsolicited
duplicate telemetry cannot extend freshness. SRXL2 telemetry has no general
source/sequence field: poll correlation and local counters are used, not an
invented wire sequence number or authenticated model identity.

There is deliberately **no hardware implementation of the bidirectional Port**.
No call from AP_Vehicle, DiagnosticRX or the receive service reaches this class.
The three independent session gates default false: electrical validation,
controller-role validation and ESC initialization validation. Non-simulated ports
also require the compile-time hardware gate. With it zero, even three asserted
runtime flags cannot activate physical TX. ELF inspection confirms the session,
planner, commands and measurement utilities are absent from vehicle firmware.
This is software preparation, not a completed native bidirectional UART driver.

## Protocol evidence and unresolved target semantics

The [official Spektrum implementation](https://github.com/SpektrumRC/SRXL2)
and [Rev K specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf)
were inspected. Existing archived upstream notices remain unchanged. New helpers
are independently implemented protocol consumers under GPL-3.0-or-later.
The archived specification SHA-256 is
`7d9c54de2c1eba38ebcf17eb2f21b8e2f1f35172ad383652bacd827f38e041d5`.

Prepared mock discovery uses controller ID 0x30, expected ESC-class ID 0x40,
CRC-valid handshake, capabilities/UID and a final baud-0 broadcast. This is not
proof of SPMXAE1130A model/firmware recognition. The older specification's
receiver-master restrictions and newer library's FC-master behavior do not
establish that this specific ESC accepts an FC-only bus. Smart Throttle and
SRXL2 are not assumed interchangeable. Required Smart Throttle initialization,
exact firmware behavior, configuration readback and target reverse channel remain
unverified. No guessed proprietary initialization or reverse command was added.

An empty-mask channel frame has documented startup/fade semantics, not a universal
motor-safe telemetry-poll guarantee. Mock polling therefore requires a separate
explicit verification argument and is not called by any runtime component.
No active PWM fallback exists. Real channel data is never passed to the mock bus
or the physical UART by this implementation.

## Telemetry and simulated command results

The Phase 3 acquisition-timestamped native receiver remains connected to the
shared protocol decoder and `AP_ESC_Telem` adapter. CRC, discovery/poll context,
stream errors, stale backlog, missing fields, timeout and frontend conflicts
gate publication. Valid ESC voltage, reported current and FET temperature feed
the existing ESC frontend, MAVLink and logging facilities. Physical publication
has not been demonstrated. Status transitions use ground-station messages;
existing SPKT diagnostic counters are logged at most once per second.

No mechanical RPM is published without verified pole count. The new pure helper
tests eRPM/(poles/2) only with verified even pole counts. Capacity requires verified
battery-input current semantics and a known baseline at the first acquired
sample; trapezoidal A*ms/3600 integration rejects missing/stale/out-of-order
coverage and does not fill gaps. These calibrations are not known for the actual
installation, so neither derived RPM nor capacity is enabled in runtime.
Unknown device/fault sensors are not fabricated or relabeled.

Channel encoding uses Rev K's channel mask, little-endian values, 16-bit values
in steps of four and CRC. Throttle endpoint/channel and optional reverse
channel/levels/dwell must be independently calibrated. Tests use explicitly
synthetic endpoints, not purported Avian defaults. There is no actual ESC reverse
configuration readback or automatic Plane/Copter mapping.

Mock cases verify disarmed, neutral, forward, reverse dwell/stopped restrictions,
disabled reverse, RC loss, ESC loss, failsafe, invalid numbers/profile/buffer and
recovery requiring fresh stopped neutral authorization. On disarm/loss/invalid
input the helper emits **no frame** and resets authorization. This is not a
physical stop-command guarantee: a future actual motor backend must implement
and validate ESC timeout/stop behavior within each vehicle's safety model.
Existing arming, failsafe and normal motor/servo logic are not bypassed.

## Actual validation evidence

Working environment: Docker Desktop Linux/WSL2, official
`ardupilot/ardupilot-dev-chibios:v0.1.3`, digest
`sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`;
Ubuntu 22.04.3, arm-none-eabi GCC 10.2.1, Python 3.10. Linux source snapshot
normalizes line endings; no Windows/BIOS change was necessary.

| Test | Actual result |
| --- | --- |
| Existing protocol/frontend GTests | 30 PASS |
| Existing receive/frontend GTests | 7 PASS |
| New Phase 4 GTests | 20 PASS |
| ASan + UBSan protocol/planner | 29 PASS |
| ASan + UBSan mock session/commands/measurements | 20 PASS |
| Generated custom board checks, including closed TX gate | 6 PASS |
| Hypothetical AUX7 UART resource checks | 6 PASS |
| Official Plane/Copter parameter metadata parsers | Both exit 0; SPKT_ENABLE/SPKT_TLM retained |
| Linked Plane/Copter firmware inspection | Custom RX present; mock TX/commands absent; stock excludes custom RX; Plane native write stub disassembly returns zero |
| Firmware build/configure exit codes | All zero; zero compiler warning lines |

Sanitizers use `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie
-no-pie`, warnings as errors, a 30-second timeout and
`ASAN_OPTIONS=abort_on_error=1:handle_segv=0`. Both instrumented binaries complete
without reported findings. These are host tests; MCU registers, physical bus
timing and motor safety were not exercised. No vehicle SITL flight was performed.

| Target | Vehicle | Flash used | Free flash | Static Data+BSS |
| --- | --- | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | Plane | 1,590,832 B | 375,240 B | 145,812 B |
| Pixhawk6C-SpktSmart | Copter | 1,594,664 B | 371,404 B | 146,908 B |
| Pixhawk6C | Plane | 1,586,640 B | 379,432 B | 141,684 B |
| Pixhawk6C | Copter | 1,590,424 B | 375,644 B | 142,780 B |

Data+BSS does not measure runtime peak stack/heap. All four builds use the normal
`./waf configure --board BOARD --out /build/firmware --no-submodule-update` and
`./waf plane` / `./waf copter -j6` workflow. Shared vehicle architecture remains
available to other applications, but no other vehicle build is claimed here.

Full logs: workspace `logs/spktsmart-phase4-linux/`, including outcomes.csv,
environment.log, six firmware configure/build logs, GTest build/run logs,
sanitizer logs, hardware checks, selected parameters and linked inspection.
The validation container `spktsmart-phase4-validation` exited 0; subsequent
resource/parameter/ELF inspection also exited 0. Artifact collector verifies
board ID 56, APJ decompressed length and exact BIN payload equality.

Actual APJ/BIN/ELF artifacts and SHA-256 manifests:

- `custom-firmware/Pixhawk6C-SpktSmart-phase4-artifacts/arduplane.apj`
- `custom-firmware/Pixhawk6C-SpktSmart-phase4-artifacts/arducopter.apj`
- `custom-firmware/Pixhawk6C-phase4-artifacts/` regression artifacts.

Reproduction: synchronize changed files into the existing `/build/src` snapshot,
mount workspace tools read-only at `/scripts` and a results directory at
`/results`, then run `bash /scripts/validate-spktsmart-phase4.sh` in the image.
Run workspace `python tools/collect_spktsmart_artifacts.py --phase 4` afterward.
The script runs four builds, three GTest binaries, both sanitizer binaries,
resource checks, official parameter generation and ELF inspection. Earlier
phase artifacts are not overwritten.

## Hardware prerequisites and next phase

1. Identify PCB revision and obtain the matching PA0-to-AUX7 circuit: buffer,
   level selection, protection, pull resistors, direction/output-enable behavior.
   If this circuit is unidirectional and cannot be released by software, stop
   AUX7 development and assess the native serial fallback.
2. With no ESC signal connected, verify connector high impedance, idle voltage,
   power-off injection/back-powering and propagation back to PA0. MCU register
   checks alone do not satisfy this gate. Measure under normal AUX8 PWM/DShot
   activity and processor load.
3. Confirm ESC signal limits and observe target firmware startup/handshake using
   suitable current-limited bench equipment. Establish whether ESC-only SRXL2
   controller operation is supported, the required initialization and whether
   any proposed poll frame can affect the throttle input.
4. Only after independent electrical/protocol review, implement a real port
   backend with verified TC/turnaround, collision detection, exclusive ownership
   and response timing. Do not merely flip the hardware-validation macro.
5. Validate receive-only captures and fresh frontend measurements before any
   physical handshake test. Later motor tests require removed propeller/EDF
   rotor, restrained equipment, an emergency power disconnect and verified
   disarm/RC-loss/ESC-loss/reboot/failsafe behavior. No such tests occurred here.
6. Obtain actual endpoint/reverse-channel settings and motor poles/current
   semantics. Then implement per-vehicle motor safety integration with explicit
   arming denial, stop-command/timeout verification and recovery restrictions.

The recommended next phase is circuit identification and instrumented receive
validation. Physical handshake, throttle and reverse remain unavailable at this
hardware boundary; the final native-control objective has not yet been achieved.

Later source-audit correction: RX-labelled pads are not categorically excluded
from native half-duplex, because H7 SWAP can exchange the effective UART data
roles. See [AUX1_AUX8_SRXL2_COMPARISON.md](AUX1_AUX8_SRXL2_COMPARISON.md) for
AUX8/PA1 and AUX1/PA8 alternatives and paired bdshot capture. Historical build
results remain unchanged; no alternate pad is hardware-qualified.
