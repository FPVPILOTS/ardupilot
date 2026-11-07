# Phase 5: AUX7 bidirectional DShot evidence and gated UART4 transport

2026-11-01. AI-assisted development continues on
`feature/pixhawk6c-spktsmart`, based on ArduPilot commit
`dbe792162d06cab66c3475fd5556bf7a120f119e`. ChibiOS gitlink:
`d103983f4362e5506bcf7062ccacf345fc0c0859`; waf gitlink:
`35eadbb64e2052099a853b571e507c33032b392c`.
Phase 5 changes remain uncommitted. Source hashes and actual artifact hashes are
recorded in the phase-5 artifact manifest. Prior work and unrelated changes were
preserved; no submodule, system firmware or BIOS was changed.

**Verdict: GO for gated software preparation; UNVERIFIED for native AUX7 SRXL2
electrical/protocol compatibility; NO-GO for physical TX or motor activation.**
No controller or ESC was flashed, connected by this agent, or energized.

## 1. What the bdshot discovery establishes

The local official [`Pixhawk6C-bdshot/hwdef.dat:15`](../Pixhawk6C-bdshot/hwdef.dat)
declares PA0/TIM5_CH1/PWM7/GPIO56 BIDIR; PA1/TIM5_CH2 remains PWM8. The
[upstream definition](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C-bdshot/hwdef.dat)
also declares this. Generated definitions allocate both TIM5 update DMA and
TIM5_CH1 capture DMA. This is meaningful evidence of an intended usable external
return path for supported Pixhawk6C hardware, rather than merely a pin with an
input-capable alternate function.

It is not a measurement on the user's board. A compiled BIDIR declaration does
not prove that every PCB revision/modification has the same buffer circuit, that
the ESC's signal is 3.3 V compatible, or that an open-drain UART can release the
connector safely. No physical bdshot test was performed here. The conclusion has
therefore improved from an unexamined possibility to an officially supported
bidirectional *timer configuration*, while the actual SRXL2 electrical gate
remains closed.

## 2. Detailed DShot versus SRXL2 comparison

| Property | ArduPilot bidirectional DShot | Prepared SRXL2 UART4 path |
| --- | --- | --- |
| Pin / alternate function | PA0 / TIM5_CH1 AF2 | Same PA0 / UART4_TX AF8 |
| Framing | Timer-generated DShot pulse widths; GCR telemetry edge decoding | Hardware UART, 115200 8N1; SRXL2 length/type/CRC16 packets |
| Output stage | bdshot setup explicitly uses push-pull, pull-up and limited slew to reduce transition noise | AF8 open-drain, no unverified added pull resistor; MCU HDSEL joins receive/transmit on TX |
| Return direction | TIM5 output enable disabled; channel reconfigured for both-edge input capture | TE disabled and PA0 temporarily input; RE enabled, then AF8 open-drain restored |
| Capture | Timer capture DMA, bounded buffer and cache invalidation/copy before decode | H7 16-byte UART RX FIFO; existing timestamp ring and bounded 1-kHz servicing |
| Resources | Timer update/capture DMA handles, locks, DMAMUX reassignment and timer-group ownership | No TIM5 or DMA register changes. Prepared TX uses one FIFO-sized diagnostic frame and gated UART4 TC interrupt |
| Completion | DMA completion initiates capture; virtual timer finishes bounded GCR transaction | Physical UART TC interrupt performs release; FIFO-ready/DMA-empty is not wire completion |
| Response timing | DShot-specific microsecond turnaround and capture duration | SRXL2 UART/handshake scheduling; no DShot pulse rate or 30-us assumption copied |
| Failure | Transaction timer, state/error counters, DMA release, PWM configuration restored as appropriate | Independent 3-ms prepared TX watchdog; shared bus response timeouts; input/RX release; explicit restart after fault/loss; never PWM fallback |
| Motor semantics | DShot commands/throttle and DShot telemetry formats | Only documented SRXL2 structures; actual Avian initialization, calibration and reverse semantics still gated |

The reusable principles are exclusive ownership, disabling the current driver
before changing direction, bounded asynchronous transactions, physical completion
handling, critical sections, retained buffers and explicit cleanup. DShot's
push-pull mode, GCR decoder, timer PSC/ARR values and waveform timing are not
appropriate substitutes for a UART transport.

## 3. Exact source references inspected

All line references below refer to this checkout's unchanged ArduPilot files:

| Source | Relevant behavior |
| --- | --- |
| `RCOutput_bdshot.cpp:78` `bdshot_setup_group_ic_DMA` | Capture DMA allocation, shared handles; around line 119, push-pull/pull-up/limited slew setup. |
| `RCOutput_bdshot.cpp:246` `bdshot_prepare_for_next_pulse` | Ownership/state preparation and shared capture/update DMA locking. |
| `RCOutput_bdshot.cpp:306` `bdshot_reset_pwm` | Restores timer output configuration after capture. |
| `RCOutput_bdshot.cpp:322` `bdshot_receive_pulses_DMAR` | Arms a virtual-timer deadline, clears timer CCER/output configuration and sets input capture/DMA. |
| `RCOutput_bdshot.cpp:388` `bdshot_config_icu_dshot` | Selects input mapping, both-edge capture and channel DMA request. |
| `RCOutput_bdshot.cpp:492` `bdshot_finish_dshot_gcr_transaction` | Bounded capture completion, DMA disable, cache invalidate/copy, DMAMUX/ownership cleanup. |
| `RCOutput_bdshot.cpp:630` `dma_up_irq_callback` | Interrupt-driven output-to-capture transition. |
| `RCOutput.cpp:1871` / `:1915` | `dma_unlock` and `dma_cancel` transaction ownership/cleanup. |
| `RCOutput_serial.cpp:39` `dshot_send_command` | Refuses output during soft-serial or capture, obtains DMA ownership before starting. Despite its filename, this file does not implement a native UART4 driver. |
| `RCOutput.cpp:1966` `serial_setup_output` | Temporary timer/group ownership for existing serial passthrough. |
| `RCOutput.cpp:2298` `serial_read_bytes` | GPIO input and edge-interrupt software serial reception; restores original line mode afterward. Not reused for safety-critical SRXL2 bit timing. |
| `RCOutput.cpp:2365` `serial_end` | Releases passthrough ownership/restores configuration. |
| `hwdef/Pixhawk6C/hwdef.dat:120` and `:121` | PA0 PWM7 and PA1 PWM8 timer mapping. |
| `hwdef/scripts/STM32H743xx.py` | PA0 TIM5_CH1 AF2, UART4_TX AF8; PA1 UART4_RX AF8. |
| `hwdef/scripts/chibios_hwdef.py:2693` | BIDIR requests timer-channel capture DMA in addition to update DMA. |
| `modules/ChibiOS/os/hal/ports/STM32/STM32H7xx/stm32_isr.h:299,311` | UART4 Vector110 / IRQ52. |

## 4. UART and board-level feasibility

[STM32 RM0433, USART single-wire half-duplex](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
documents HDSEL linking TX and RX internally, with the separate RX pin unused.
UART4 can therefore transmit and receive through PA0, without consuming PA1.
Half-duplex uses independent TE/RE controls; clocked/smartcard/infrared modes
are not enabled. Open-drain release requires a suitable actual pull-up.
These are MCU properties, not proof of connector impedance or signal voltage.

The [official SRXL2 specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf)
electrical section describes a 3.3 V idle-high signal and high-impedance idle
with weak pull-up. Signal voltage is separate from servo-bus supply voltage.
The archived Rev K PDF hash remains
`7d9c54de2c1eba38ebcf17eb2f21b8e2f1f35172ad383652bacd827f38e041d5`.

The user's PCB revision remains unknown. [Holybro voltage modification guidance](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod)
distinguishes RC09, RC12 and RC13; notably RC13 no longer offers AUX 5-V
modification. A default/unmodified assumption is not an electrical measurement.
The [connector documentation](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
identifies AUX7 at FMU PWM OUT signal pin 8, but does not specify the complete
revision-matched buffer/protection circuit. Buffer direction, enable behavior,
clamp/back-powering paths and open-drain propagation are still unverified.

UART4 and TIM5 are different peripherals. Moving **PA0 only** to AF8 excludes
simultaneous TIM5_CH1 output on that pad; firmware already reserves AUX7 outside
PWM/GPIO/SERIALn. PA1 stays AF2/TIM5_CH2/PWM8, and its existing timer/DMA resources
remain allocated. This differs from DShot's timer-group capture reconfiguration,
which operates on the shared timer. The new backend never changes TIM5 or PA1.
Generated resource checks establish configuration consistency, not measured
PWM8 waveform quality under load.

## 5. Implemented changes

### Prepared native port

New [`SpektrumUART4.h`](../../SpektrumUART4.h) and
[`SpektrumUART4.cpp`](../../SpektrumUART4.cpp) implement the existing
`AP_SpektrumSmartESC_Bus::Port` contract using the existing `DiagnosticRX` instance.
They do not create another independent UART4 receiver or modify SerialManager.

- Acquisition requires the compile-time hardware gate and main-thread context,
  no active prepared port, and an uninitialized/unleased receiver. It does not
  interrupt an existing passive receiver to acquire the port.
- UART initialization reuses the existing 115200/FIFO/HDSEL RX startup. ChibiOS
  `sdStart` is still avoided because its startup enables TE.
- `begin_tx` requires idle-high, no UART BUSY, no unread software/FIFO receive
  data, valid CRC and a strict diagnostic-packet whitelist. It accepts only the
  existing 14-byte controller handshake or explicitly validated zero-mask poll
  experiment. It refuses every throttle/reverse/channel-value frame.
- TX disables RE, clears TC, enables TE, waits non-blockingly for TEACK and
  feeds at most 16 bytes to the hardware FIFO. No DMA or bit-banging is added.
- Once queued, TCIE requests an IRQ. The prepared handler releases PA0 to input,
  disables TE/TCIE, clears/flushed UART state, enables RE and restores AF8
  open-drain. It latches completion only after that release. A millisecond
  vehicle task is not used for the critical TX-to-RX direction change.
- A ChibiOS virtual timer, following the bounded bdshot transaction pattern,
  aborts/releases after 3 ms even if the main thread stops servicing TX. Bus
  response timeouts remain separate. Release/teardown never enables PWM.
- UART errors, accumulated receiver errors or unexpected pinmux/HDSEL state
  fault the prepared port. Idle/BUSY checks prevent known overlap. **This is not
  reliable detection of electrical contention during HDSEL transmission.** No
  software collision guarantee is claimed for an unmeasured electrical bus.
- Ownership is retained through the buffer lifetime, TC or abort. Failed start
  cleans up its lease. Destruction/relinquish releases the line before disabling
  UART4 and returns PA0 to input.

**Physical activation remains impossible in the supplied builds:**
`AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED` is still zero. Acquisition and TX
both refuse operation. The UART4 IRQ wrapper is not installed at gate zero.
No AP_Vehicle code instantiates this port or connects the mock bus to it.
ELF inspection confirms `SpektrumUART4`, the bidirectional session and command
generators are absent from Plane/Copter runtime. Enabling it later requires
hardware/protocol review and integration, not simply a parameter or macro flip.
The UART4 register helper bodies were compiled against the real H743 headers;
the gate-enabled vector wrapper and live register execution were not tested.

### Existing native receiver

`DiagnosticRX.{h,cpp}` adds the shared experimental lease and a fail-closed
runtime check for unexpected TE, lost HDSEL or wrong PA0 mode/AF/open-drain.
On mismatch it disconnects PA0, disables UART4, purges buffered data and increments
the existing error counter. That counter already causes the receive/telemetry
service to withdraw observations. This guard remains active in RX-only builds.
The public UART write stub still returns zero and diagnostic parameters retain
disabled defaults. This register guard is build-checked; hardware fault injection
is not claimed.

### Shared protocol and scheduler

`AP_SpektrumSmartESC_Bus.{h,cpp}` adds the CRC-checked diagnostic whitelist and
an explicitly authorized **mock-only** 10–25-ms poll scheduler. It is off by
default, refuses non-simulated ports, does not overlap an outstanding transaction,
does not catch up in bursts after a busy bus, and clears authorization on loss,
fault, disable or restart. Existing identity, timeout, master-yield and CRC rules
remain. Seven tests were added to `tests/test_spektrum_phase4.cpp`, preserving
all 20 previous tests. The existing filename/target was retained deliberately.

`tests/verify_bdshot_path.py` adds six generated checks against the official
bdshot definition. It locates TIM5 by driver identity because DMA ordering gives
bdshot different generated group numbering from stock/custom definitions.
Workspace helpers add `validate-spktsmart-phase5.sh` and
`inspect_spktsmart_phase5.py`; the artifact collector now accepts phase 5 and
hashes the new HAL files. No upstream RCOutput or stock board definition changed.

## 6. Spektrum scope and remaining protocol gates

The [official Spektrum source](https://github.com/SpektrumRC/SRXL2) remains the
protocol reference; existing upstream license notices are preserved. The new
port consumes existing independently implemented packet helpers. It does not
replace them with DShot frames or mix DSM receiver decoding with ESC control.

Generic ESC-class ID 0x40, UID and capability bits do not identify SPMXAE1130A or
its firmware revision. The older receiver-master specification and newer FC
master library support do not prove that this exact Avian accepts an ESC-only
FC-master bus. Smart Throttle-specific initialization remains unknown. No
proprietary initialization, guessed configuration readback or reverse command
was added. Empty-mask channel frames have startup/fade semantics; their use as
a harmless ESC telemetry poll is still a separate target-verification gate.

The existing simulated command helper still requires explicit calibrated
channels/endpoints and, for reverse, verified channel/levels and stopped-neutral
dwell. Loss/disarm/failsafe resets authorization and emits no frame. That is not
a physical ESC stop guarantee. Physical throttle/reverse and vehicle arming
integration remain unavailable. Normal Plane/Copter motor safety is unchanged.

Telemetry continues through the existing acquisition-timestamped RX parser and
AP_ESC_Telem adapter, gated by CRC, discovery/poll context and freshness. The
runtime publishes only supported voltage, reported current and FET temperature.
Mechanical RPM needs actual motor poles; capacity needs confirmed battery-input
current semantics and continuous coverage. The tested calibrated helpers remain
unconfigured/unpublished. Unknown status/fault fields are not fabricated. No
physical telemetry or Mission Planner display was demonstrated.

## 7. Tests, builds and evidence

Final validation uses the existing official Linux image
`ardupilot/ardupilot-dev-chibios:v0.1.3`, digest
`sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`,
Docker Desktop/WSL2, GCC Arm 10.2.1 and Python 3.10.12. The normal waf
configure/build workflow is retained. No system/BIOS changes were necessary.

| Check | Actual final result |
| --- | --- |
| Protocol/frontend GTests | 30 PASS |
| Receive/frontend GTests | 7 PASS |
| Mock bus/commands/measurements and new Phase 5 GTests | 27 PASS (20 retained + 7 new) |
| ASan/UBSan protocol/planner | 29 PASS |
| ASan/UBSan Phase 4/5 mock suite | 27 PASS |
| Existing custom/candidate resource checks | 12 PASS |
| New official bdshot resource checks | 6 PASS |
| Official Plane/Copter parameter generation | Both exit 0; existing disabled SPKT defaults retained |
| Linked symbols | Both custom vehicles exclude prepared native TX port/session/commands; stock excludes custom RX |
| Native RX write disassembly | Returns zero |
| Target-object port acquisition disassembly | Gate-zero acquire returns false, no peripheral stores or calls |

Totals: **64 GTests, 56 ASan/UBSan cases, 18 resource checks.** Sanitizer binaries
use address+undefined instrumentation, non-PIE linking, warnings as errors,
30-second timeout and `ASAN_OPTIONS=abort_on_error=1:handle_segv=0`. No findings
were reported. These do not execute STM32 registers or measure the electrical
bus. No SITL flight, physical motor/failsafe test or hardware compatibility test
was performed.

| Target | Vehicle | Flash used | Free flash | Static Data+BSS | Compiler warnings |
| --- | --- | ---: | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | Plane | 1,590,908 B | 375,160 B | 145,824 B | 0 |
| Pixhawk6C-SpktSmart | Copter | 1,594,740 B | 371,332 B | 146,912 B | 0 |
| Pixhawk6C | Plane | 1,586,640 B | 379,432 B | 141,684 B | 0 |
| Pixhawk6C | Copter | 1,590,424 B | 375,644 B | 142,780 B | 0 |

Static RAM does not include runtime peak heap/stack. Build success is established
by actual compile/link logs, not configuration alone. `SpektrumUART4.cpp` appears
in the custom target's compile log, while linker dead-stripping keeps it out of
runtime. Standard Pixhawk6C retains its existing behavior and sizes.

Workspace evidence: `logs/spktsmart-phase5-linux/`, including all configure/build
logs, environment/outcomes, three GTest logs, both sanitizer logs, parameter
metadata, resource checks and ELF inspection. Intermediate runs are retained in
`logs/spktsmart-phase5-attempt1` through `attempt4`. One iteration caught ChibiOS
GPIO's actual `AFRL` name rather than CMSIS `AFR[0]`; another exposed the different
bdshot group ordering. These intermediate failures are not counted as passes.
Final validation container `spktsmart-phase5-final4` exited zero. The subsequent
extended gate-object disassembly inspection also exited zero; its output is
`native-port-gate-disassembly.txt`.

Final APJ/BIN/ELF files and SHA-256 manifests are in
`custom-firmware/Pixhawk6C-SpktSmart-phase5-artifacts/` and
`custom-firmware/Pixhawk6C-phase5-artifacts/`. The collector checks board ID 56,
decompressed APJ length and exact BIN payload equality. Earlier phase artifacts
remain unchanged. To reproduce, synchronize changed sources to the existing
Linux `/build/src` volume, mount tools at `/scripts` and logs at `/results`, run
`bash /scripts/validate-spktsmart-phase5.sh`, then run workspace
`python tools/collect_spktsmart_artifacts.py --phase 5` after exit 0.

## 8. Extra hardware, exact measurements and next steps

**No converter, microcontroller or receiver has been shown necessary.** The
official bdshot support justifies pursuing AUX7 directly. It also does not prove
that no pull-up, buffer change or other hardware intervention will be needed.
The preferred one-signal-plus-common-ground architecture remains plausible and
unverified. No extra hardware is prescribed or implemented in this phase.

Before any ESC signal connection or TX enablement:

1. Identify PCB revision and obtain/trace the exact PA0-to-AUX7 circuit, including
   any buffer, output-enable, voltage-select pads, resistors and protection.
   Establish which component, if any, controls signal direction.
2. Measure connector voltage, powered/unpowered leakage/back-powering and output
   impedance in boot, GPIO-input, AF8 RX-only and disabled states. Confirm that
   setting MCU input actually releases the external connector.
3. With suitable isolated/current-limited bench equipment and no ESC initially,
   verify an externally imposed safe logic signal reaches PA0/UART4. Measure
   idle bias, rise/fall time and loading; establish whether existing pull-up
   bias is sufficient at 115200. Do not guess a resistor value or enable 400000.
4. Verify ESC-side signal voltage/idle behavior and startup captures for this
   exact SPMXAE1130A firmware. Establish FC-only discovery/handshake behavior and
   the semantics of proposed diagnostic polls before sending them.
5. After an independent electrical review, instrument TE/RE, actual last stop
   bit, TC IRQ entry, pin release and first received byte under processor load.
   Measure IRQ latency, REACK readiness, RX FIFO/1-kHz servicing margin, framing
   errors, contention and watchdog behavior. Current millisecond RX timestamps
   cannot certify sub-millisecond turnaround; future bus integration must use
   an appropriate acquisition/completion timebase and drain order.
6. Confirm AUX8 PWM waveform/rate/DMA behavior remains intact while UART4 is
   active. Exercise stuck-low, unexpected master/device, truncated/bad CRC,
   disconnect/reboot, clock/pinmux faults and timeout recovery. A register guard
   does not prove electrical contention protection.
7. Only then implement the runtime port factory/frame pump, validate the
   gate-enabled IRQ wrapper and scheduler on hardware, and review protocol gates.
   Do not simply flip the hardware macro. Physical throttle/reverse require
   actual endpoint/reverse configuration and per-vehicle arming/stop/failsafe
   integration. Initial powered motor tests must have no propeller or EDF rotor.

If the AUX7 circuit proves unsuitable, AUX8's PA1 is UART4_RX rather than the
HDSEL TX pin and is not an equivalent substitute. Other AUX functions do not
offer a clearly better free UART TX path in the existing mapping. The next
native connector candidate remains TELEM3/USART2 (PD5 TX/PA3 RX, SERIAL5), with
its own electrical/ownership assessment; joining push-pull TX/RX wires is not
an approved fallback. No conclusion that an external converter is required can
be made before that assessment.

The next development phase is instrumented electrical and receive validation,
followed by reviewed diagnostic-only native integration. The final native
throttle/reverse objective is not achieved or flight-ready at this boundary.

Later source-audit correction: RX-labelled pads are not categorically excluded
from native half-duplex, because H7 SWAP can exchange the effective UART data
roles. See [AUX1_AUX8_SRXL2_COMPARISON.md](AUX1_AUX8_SRXL2_COMPARISON.md) for
AUX8/PA1 and AUX1/PA8 alternatives and paired bdshot capture. Historical build
results remain unchanged; no alternate pad is hardware-qualified.
