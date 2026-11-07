# Phase 6: electrical qualification and receive-only bench diagnostics

2026-11-01, Europe/Amsterdam. AI-assisted work continues on
`feature/pixhawk6c-spktsmart`, base commit
`dbe792162d06cab66c3475fd5556bf7a120f119e`. Changes remain uncommitted.
Phase 5 sources/artifacts/logs are retained. No flight controller was flashed,
no electrical measurement was performed and no ESC or test generator was
connected or powered by this agent.

**AUX7 verdict: UNVERIFIED. NO-GO for ESC connection, injected test signals or
physical TX until the relevant electrical review below is complete.**
Software preparation is not a successful electrical validation. Extra flight
hardware has not been shown necessary, and a converter is not prescribed.

## 1. Hardware evidence and PCB requirements

The [Holybro port documentation](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
and its pin-order/pinout diagrams were inspected. FMU PWM OUT is AUX, while
I/O PWM OUT is MAIN. The AUX connector assigns pin 8 to FMU_CH7, pin 9 to
FMU_CH8 and pin 10 to ground. Nominal AUX logic is documented as 3.3 V.

[Holybro's revision guidance](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod)
and RC09/RC12 layout renderings were inspected. RC09 illustrates 5V/3V3 selection
pads; RC12 identifies JP1 (bridged selects 5 V, unbridged 3.3 V). RC13 no longer
supports AUX 5-V modification. These illustrations do not provide the complete
PA0-to-connector netlist, buffer part number or output-enable/direction circuit.
No resistor, buffer IC or protection rating is inferred from its footprint.

There is **no qualified revision whitelist**. Before electrical stimulation,
record actual PCB revision, serial number, voltage-selection state, component
population and any repairs/modifications. The user's revision remains unknown
and there is no known deliberate modification. Do not equate that with verified
unmodified hardware. A matching schematic/BOM or manufacturer-confirmed signal
path and component ratings is required; RC09, RC12 and RC13 are not automatic GO.
The [public downloads](https://docs.holybro.com/autopilot/pixhawk-6c/download)
do not establish a revision-matched electrical schematic in the inspected pages.

The official `Pixhawk6C-bdshot/hwdef.dat:15` declares
`PA0 TIM5_CH1 TIM5 PWM(7) GPIO(56) BIDIR`. The driver switches TIM5 from output
to input capture and DMA. This is evidence of an intended return path on supported
boards, not a measurement on this unit. Its push-pull output and timer capture
cannot prove open-drain UART behavior. No board-specific external buffer-direction
control was found in the inspected hwdef/bdshot path. That absence is not proof
that no external buffer exists.

At the MCU, PA0 supports TIM5_CH1 AF2 and UART4_TX AF8. Single-wire HDSEL joins
UART TX/RX internally on TX, leaving PA1 unused by UART4. These properties are
documented in [STM32 RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
and the checkout's STM32H743 alternate-function table. If the external return
path propagates safely and independently of the timer mode, it can in principle
reach the UART input on the same PA0 pad. Neither condition is proven here.
UART4 does not share TIM5 registers; PA1/TIM5_CH2 remains PWM8. The existing
resource evidence is retained from Phase 5, not rerun as a physical test.

Archived official illustrations are in workspace `sources/phase6-hardware/`:
`holybro-pin-order.jpg`, `holybro-pinout.png`, `holybro-rc09.png`,
`holybro-rc12.png`. They are reference illustrations, not photos of the user's
controller. They must not be used to claim a measured component value or net.

## 2. Exact connector orientation

Use the 10-position **FMU PWM OUT**, not the neighboring I/O PWM OUT. In the
published top/pinout drawing with the nose arrow upward, FMU PWM OUT is at the
lower left. Match the actual housing and keyed connector before numbering.

Holybro's pin-order diagram puts pin 1 on the right in its illustrated connector
view. Extended to the ten positions, in that **same view**:

```text
left                                                     right
 [10] [9] [8] [7] [6] [5] [4] [3] [2] [1]
  GND AUX8 AUX7                         VDD_Servo
```

This is not an instruction to count from an arbitrary cable/backside view.
The cable's wire-entry view can be mirrored. Pin 1 is the documented red
VDD_Servo conductor; verify an adapter's mapping separately while disconnected
from the controller. Do not assume black wire means ground: pins 2–10 are all
documented black. Only pin 10 is the ground reference for this procedure.
Pin 1 is servo-rail power, not the SRXL2 signal and not the signal pull-up supply.

## 3. Safe diagnostic firmware changes

Physical TX gate remains `AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED=0`.
There is no parameter, script command or MAVLink command to authorize TX. The
prepared Phase 5 port is not instantiated. No throttle, reverse, channel-data,
telemetry poll or handshake is transmitted by Phase 6 diagnostics.

Existing `SPKT_ENABLE=1` is now explicitly a receive-only bench mode. Default
remains zero. A nonzero or invalid value vetoes arming; observing any nonzero
value latches the veto until disabled **and rebooted**. Invalid values do not
start UART4. Changes remain reboot-required; live enabling does not start a
receiver. A failed/unavailable receiver does not remove the arming veto.

- Shared `AP_Arming` checks veto normal, skip-check and forced arm requests.
  `ARMING_REQUIRE=0` does not implicitly arm a bench started disarmed. Existing
  already-armed/soft-armed vehicles are preserved if a reboot-required parameter
  is edited in flight; RX never starts or continues while soft-armed.
- When the bench is active while disarmed, diagnostics asserts the existing
  SRV motor emergency stop and keeps it asserted until reboot, including after
  ENABLE is cleared. It does not invent motor-specific PWM commands. Arming
  veto and normal motor suppression are not a physical isolation switch: raw
  GPIO, passthrough, scripts and ordinary non-motor servo signals still require
  disconnected hardware and the preparation below.
- The native receiver remains TE-clear, HDSEL, AF8 open-drain/no-pull. On wrong
  mux/HDSEL/unexpected TE it detaches PA0 and disables UART4. The receive service
  now detects native shutdown, stops observation and withdraws telemetry.
- `UARTDriver::ReceiveDiagnostics` is an optional snapshot API. Other UARTs
  return unsupported. H7 snapshots use a critical section and do not read UART
  registers with its clock disabled.
- New `SPKT_EXP` (parameter index 3, default 0) labels experiments. It authorizes
  no operation. Existing ENABLE/TLM indices 1/2 are unchanged. Negative labels
  are recorded as zero. The parameter is not an electrical-validation flag.

Existing protocol log `SPKT` is retained. New `SPKH`, at most once per second,
records:

| Field | Meaning / limitation |
| --- | --- |
| TimeUS, Exp | Boot timestamp and operator's experiment label; also record firmware hash/boot externally. |
| Flags | bit 0 receiver initialized; bit 1 PA0 sampled high; bit 2 GPIO input; bit 3 AF8 open-drain. A high pad is not a voltage/impedance measurement. |
| CR1, CR3, ISR | Current UART register snapshot while initialized; zero when clock disabled, not a capture of the last fault's registers. |
| Frame, Noise, Parity, Overrun | Counts of observed hardware error-flag events. Sticky flags mean these are not guaranteed counts of every corrupted byte. |
| Overflow | Software receive-ring overflow count. |
| RelErr | Software startup/configuration-release faults, not physical high-impedance or output-buffer contention errors. |
| RxTmo | Entries into 50-ms receive-silence intervals, once per quiet interval. It is not a count of failed handshakes or proof an ESC was present. |

Counters are cumulative within a boot and do not reset when EXP changes. Use
before/after deltas per experiment. Logs require normal disarmed logging to be
configured; set `LOG_DISARMED=1` manually for a reviewed bench session. No
logging parameter is silently changed. Logging or software status alone cannot
qualify an external buffer. No hardware logs have been obtained in this phase.

Changed sources: `AP_Arming.{h,cpp}`, `AP_Vehicle.h`, `UARTDriver.h`,
`DiagnosticRX.{h,cpp}`, `AP_SpektrumSmartESC_Diagnostics.{h,cpp}`,
`AP_SpektrumSmartESC_Receive.cpp`; new `AP_SpektrumSmartESC_Bench.h` and
`tests/test_spektrum_bench.cpp`; one native-shutdown case added to the existing
receive test target. Workspace validation/inspection/artifact helpers support
Phase 6. Existing protocol framing/command generators and stock hwdefs are not
rewritten. The separate measured build/test record follows after validation.

## 4. Instrumentation and approval boundaries

Prepare a high-impedance oscilloscope with suitable voltage range and calibrated
probe attenuation; a two-channel scope is preferable. A compatible logic analyzer
can decode UART after voltage is known, but cannot establish analog levels,
high impedance, overshoot or contention current. Record analyzer threshold,
sample rate and any internal pull-ups. Initially do not attach a test generator,
pull resistor, ESC, receiver, BEC or external bias supply to AUX7.

Remove all motor/servo power and control connections before powering the board.
No propeller or EDF rotor may be installed for later powered motor-related work.
Use a supported current-limited controller power source, with servo rail isolated.
Do not join USB supply and an ESC BEC/servo supply without a reviewed power plan.
Verify scope earth/USB grounds and supply isolation; never defeat protective
earth. Ground clips go only to verified GND, not a signal or power pin.

The initial permitted measurements are observational only: board identity,
documented connector mapping and high-impedance voltage observation of the board
alone. Resistance/diode/continuity tests inject current: do not perform them on
the unknown board signal path before ratings are established. A disconnected
adapter/cable may be mapped separately without the board attached.

All active/loading tests below are **conditional future procedures**, not
authorization to inject a signal now. Before them, a qualified reviewer must
record component ratings, board path, allowable stimulus/impedance/current,
power sequencing, probe loading and abort limits. Record manual authorization
for the specific experiment. The review must precede both active injection and
any ESC signal connection. This document has no completed authorizations.

## 5. Measurement procedure and decision criteria

Use the companion `PHASE6_MEASUREMENTS.csv`; every experiment is initially
NOT_RUN. Save raw scope captures, screenshots, instrument settings, parameter
file, APJ SHA-256, SPKT/SPKH log and reviewer decision. Set SPKT_EXP to the
experiment number and record start/end counter snapshots where available.

| ID | Measurement / setup | Required evidence and decision |
| --- | --- | --- |
| E00 | Identify PCB, revision/serial, JP/pad state, components and adapter pin mapping. Board unpowered; no signal stimulation. | Revision-matched AUX7 schematic/net evidence and ratings. If unavailable, remain UNVERIFIED and do not progress to loading/injection/ESC. |
| E01 | Board alone, no ESC; scope AUX7 pin 8 versus GND pin 10 through boot with SPKT_ENABLE=0. | Capture min/max voltage, startup transitions, powered/off behavior and power source. No assumed idle level; input may float. Stop if wiring/ground cannot be established. |
| E02 | Board alone; manually select SPKT_ENABLE=1, EXP=2, disarmed logging, reboot. Observe AUX7 without external bias. | Confirm arming veto, UART status, TE clear and receiver flags/logs. Compare idle voltage to E01. This proves neither receiver propagation nor high impedance. No forced-arming test on attached motors. |
| E03 | After path/tolerance review only: assess release using reviewer-approved passive loads/bias and compare input versus RX-only state. No ESC. | Measure voltage/current, rise/fall and settling under specified load. Derive effective impedance only with a valid circuit model. Identify a strong always-enabled buffer or clamp. No arbitrary resistor value is supplied for an unknown circuit. |
| E04 | After stimulus limits are approved only: generator sends known 115200 8N1 UART data to AUX7, with reviewed current limiting and common reference. | Measure at connector and, if safely accessible, an identified MCU-side test node. Correlate generated byte count, RX byte deltas and errors. No unidentified MCU-pad probing. Validate input VIH/VIL against the actual path, not nominal 3.3 V alone. |
| E05 | Output-buffer direction: using qualified signals/states only, compare connector and identified MCU-side node during both directions and idle. | Establish true return propagation, release, buffer enable behavior, delay, overshoot and power-off injection. If the buffer is unidirectional or cannot release, AUX7 is NO-GO without redesign; assess native UART fallback. |
| E06 | Future separately reviewed TX firmware only, no ESC: one diagnostic handshake into a qualified passive load. Phase 6 firmware cannot perform this test. | Explicit manual authorization and bounded one-shot code review. Capture 115200 bit period (~8.68 us), start/stop bits, TC IRQ/release, RE readiness and abort behavior. No channel-data frame. Measure at both nodes if qualified; verify no driven-low or strong driven-high idle. |
| E07 | Contention protection: first analyze waveform overlap and current limits; no deliberate opposing push-pull drivers. | Prove ownership/turnaround margin with safe instrumentation. UART error flags do not reliably detect physical contention during HDSEL TX. Stop on unexpected levels/current; no forced bus fight. If detection/protection is inadequate, do not approve shared-bus TX. |
| E08 | AUX8 independence: scope pin 9 versus pin 10 while AUX7 transitions through qualified diagnostic states. All actuators disconnected. | Under a separately approved AUX8-only bench pattern (no motor function), compare waveform/rate/jitter and check UART errors. Current receive-only firmware does not authorize a new pin stimulus or override safety/motor stop to create this pattern. UART4 must not claim PA1/TIM5 or disturb other outputs. |

Do not set blanket voltage/current thresholds from an MCU pin's nominal
5-V-tolerance label. The weakest buffer/protection/power-state rating governs.
For a 3.3-V SRXL2 bus, any observed 5-V signal or unresolved clamp/back-power path
is a stop condition until corrected and reviewed. Quantitative VIH/VIL,
overshoot, drive current and allowable rise time must come from identified parts
and measured loading. Existing weak bias may suffice or may not; do not assume
that an additional resistor is unnecessary or safe.

## 6. Receiver-managed SRXL2 and first ESC communication

The [official Spektrum implementation](https://github.com/SpektrumRC/SRXL2)
and archived Rev K specification remain the reference. A receiver normally
discovers devices, negotiates baud, broadcasts channel data and selects a
telemetry ReplyID. Only the polled device responds. The newer source explicitly
handles a broadcast master in the FC ID class (`spm_srxl.c`, broadcast handshake
around line 867); this does not prove SPMXAE1130A firmware accepts an FC-only bus.

To emulate a master without an installed receiver, Pixhawk would need reviewed
discovery/ID ownership, baud agreement, poll scheduling/response windows,
freshness/timeouts and coexistence behavior. Prepared software covers mock
parts of this, but exact Avian initialization/configuration and reverse settings
remain unknown. Generic ESC ID 0x40/UID/capabilities are not model/firmware
identification. No additional Smart Throttle initialization is guessed.

For the **first possible physical communication**, after E00–E08 qualification:

1. Obtain separate explicit manual approval, record ESC model/firmware/BEC/power
   wiring, remove propeller/rotor and provide an emergency power disconnect.
2. Qualify the ESC's own signal voltage/idle behavior with approved instrumentation
   before joining it to the Pixhawk. Do not back-feed controller/servo power.
3. Start with receive-only capture of an ESC-originated announcement if present.
   Establish whether it is SRXL2 at 115200; do not infer Smart Throttle merely
   from a servo connector or telemetry label.
4. Review a one-shot handshake-only experiment: existing serializer format
   `A6 21 0E 30 40 14 00 00 UID0 UID1 UID2 UID3 CRC_H CRC_L`, using an assigned
   unique controller UID and correct CRC. This is a protocol template, not a
   ready-to-transmit packet or an authorization. No synthetic UID fixture is
   claimed to be this Pixhawk's identity.
5. No channel packet (including zero-mask polling), reverse, bind, programming
   or configuration write is allowed in that first experiment. No retries or
   final broadcast unless separately reviewed and authorized. The current
   general mock discovery session must not be connected blindly to hardware.
6. Verify actual voltage, wire bytes, release and ESC response. On no response,
   bad framing, unexpected master/UID, reset or contention: return to input/RX
   and stop. Never fall back to PWM. A packet named handshake is not proof it
   cannot be misinterpreted by a PWM-capable ESC in another startup mode.

The current build exposes no mechanism to execute step 4. A future diagnostic
TX change needs code and electrical review in addition to manual authorization;
changing the existing compile-time gate alone is insufficient.

## 7. Hardware checklist and current decision

- [ ] Exact PCB revision and voltage-selection state recorded.
- [ ] Revision-matched AUX7 buffer/protection path and ratings established.
- [ ] Connector view/pin mapping verified on actual cable/board.
- [ ] E01/E02 passive captures and diagnostic log collected.
- [ ] Approved E03 release/impedance and E04 receive evidence collected.
- [ ] E05 buffer direction and power-state behavior qualified.
- [ ] E06/E07 reviewed TX turnaround/protection evidence collected.
- [ ] E08 AUX8 independence measured.
- [ ] Exact ESC voltage/startup/protocol behavior established.
- [ ] Explicit authorization for first bounded handshake recorded.

All boxes remain unchecked. AUX7 is **UNVERIFIED**, and physical connection/TX
is **NO-GO pending qualification**, not declared technically impossible. The
software cannot decide whether extra flight hardware is required. If AUX7 is
proven unsuitable, native TELEM3/USART2 remains the next candidate, subject to
its own buffer/voltage/ownership review; joining UART TX/RX outputs is not an
approved substitute. No converter or receiver has been introduced.

Work stops at the physical-measurement/manual-hardware boundary. No hardware
pass, electrical tolerance, oscilloscope trace, ESC response or flight-readiness
claim is made. Software/build evidence is recorded separately below.

## 8. Actual software validation and diagnostic artifacts

Only tests relevant to the new bench policy, arming integration and changed
receive lifecycle were run. The unchanged Phase 5 protocol/mock matrix and 18
resource checks were not repeated. The shared UART virtual interface and arming
changes affect both custom and standard firmware, so these four affected images
were compiled and linked using the existing Docker/WSL2 Linux environment.

| Check | Actual result |
| --- | --- |
| New bench policy and actual AP_Arming host integration | 7 GTests PASS |
| Changed receive service, including native-shutdown regression | 8 GTests PASS (7 relevant existing cases plus 1 new) |
| New pure bench policy under ASan/UBSan | 6 PASS |
| Plane and Copter official parameter metadata parser | Both exit 0; SPKT_ENABLE, SPKT_TLM and SPKT_EXP emitted |
| Custom Plane/Copter ELF | Bench arming veto and native receive diagnostics linked; physical TX port/session/commands absent |
| Standard Plane/Copter ELF | Custom receiver and bench veto absent |
| Native RX write disassembly | Returns zero |
| Target-compiled experimental port acquire at gate zero | Returns false without peripheral writes/calls |
| Firmware warnings | Zero compiler warning lines in all four final build logs |

The arming test uses actual AP_Arming entry points, a minimal AP_Vehicle and
zero-channel GCS/RC fixtures with diagnostics compiled on. It verifies default
ARMING_REQUIRE=0 behavior, bench veto before init, normal and forced-arm refusal,
no automatic disarm for an already armed/soft-armed parameter edit, invalid-mode
veto, the SRV emergency-stop latch and disabled-but-not-rebooted refusal. No real
MAVLink command, RC receiver or flight loop was exercised. The native UART
register code was target-compiled, not executed on a host or MCU.

Timeout tests cover no ESC/no traffic, one event per quiet interval, acquisition
timestamps including an old first batch, inactivity and clock wrap. Sanitizers
retain the established address+undefined, non-PIE, warnings-as-errors build,
30-second timeout and `ASAN_OPTIONS=abort_on_error=1:handle_segv=0` settings.
They cover the new pure bench policy, not hardware registers or the complete
vehicle. The GTest binaries are bounded by a 45-second execution timeout.

| Target | Vehicle | Flash used | Free flash | Static Data+BSS |
| --- | --- | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | Plane | 1,591,916 B | 374,152 B | 145,872 B |
| Pixhawk6C-SpktSmart | Copter | 1,595,704 B | 370,364 B | 146,964 B |
| Pixhawk6C | Plane | 1,586,660 B | 379,408 B | 141,688 B |
| Pixhawk6C | Copter | 1,590,444 B | 375,628 B | 142,776 B |

Data+BSS is static RAM, not peak heap/stack usage. All configure and build exits
are zero. The official image remains `ardupilot/ardupilot-dev-chibios:v0.1.3`,
digest `sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`,
GCC Arm 10.2.1 / Python 3.10.12. Final container
`spktsmart-phase6-final2` exited zero; extended metadata/ELF inspection exited
zero afterward. Source snapshot normalization is confined to the Linux volume.

Evidence is in workspace `logs/spktsmart-phase6-linux/`; intermediate failures
remain in `spktsmart-phase6-attempt1` through `attempt3`. The initial test-only
GCS-disabled configuration exposed unrelated AutoTune build assumptions. With
ordinary GCS enabled, the minimal host fixture needed an RC singleton and its
parameter table for status reporting. The retained backtrace describes that
earlier fixture failure, not a hardware or final firmware result. None of these
intermediate failures is counted as passing. The subsequent successful runs
use normal GCS code with zero physical channels.

Diagnostic APJ/BIN/ELF files and SHA-256 manifests are in workspace:

- `custom-firmware/Pixhawk6C-SpktSmart-phase6-artifacts/arduplane.apj`
- `custom-firmware/Pixhawk6C-SpktSmart-phase6-artifacts/arducopter.apj`
- `custom-firmware/Pixhawk6C-phase6-artifacts/` contains regression artifacts.

The collector verifies board ID 56, decoded APJ image length and exact BIN
equality. Defaults remain SPKT_ENABLE=0 and SPKT_TLM=0. These are gated bench
preparation images, not qualified electrical TX or flight firmware. Their presence
does not authorize flashing or any hardware operation.

Reproduction helpers: `tools/validate-spktsmart-phase6.sh` validates affected
builds and focused tests; its `tests-only` argument avoids rebuilding unchanged
images during test-fixture work. `tools/inspect_spktsmart_phase6.py` inspects the
resulting images and generated parameter metadata. The existing collector
accepts `--phase 6`. No unchanged broad protocol suite is included in this
phase's validation helper. Hardware records remain empty until an operator
completes the reviewed measurement procedure.
