# Phase 7: AUX7 bench bring-up and physical stop gates

2026-11-01, Europe/Amsterdam. Continue Phase 6 on AUX7/PA0/UART4 AF8; no output
migration. Base commit `dbe792162d06cab66c3475fd5556bf7a120f119e`, branch
`feature/pixhawk6c-spktsmart`. Changes remain uncommitted and AI-assisted.

**Current decision: UNVERIFIED electrical interface; NO-GO for ESC connection,
external test-signal injection or physical TX until the relevant gate below is
approved.** No hardware was flashed, measured or energized by this agent. No
electrical evidence was supplied. All measurement entries remain NOT_RUN.

## 1. Revision evidence: what is and is not known

Use the existing [Phase 6 evidence](PHASE6_HARDWARE_VALIDATION.md) and archived
Holybro illustrations; the AUX comparison is unchanged. A targeted check of
Holybro's available documentation/download listings found no revision-matched
Pixhawk6C AUX7 electrical schematic. The
[6C download page](https://docs.holybro.com/autopilot/pixhawk-6c/download)
lists case/adapter CAD, which cannot establish buffer direction. A 6X/baseboard
or generic FMUv6C reference schematic is not evidence of this board's circuit.

| Actual revision | Documented voltage-selection evidence | AUX7 buffer / return path |
| --- | --- | --- |
| Unknown / earlier revision | No applicable configuration established | UNKNOWN; identify the actual board first |
| RC09 | Published 5V/3V3 selection-pad illustration | Exact AUX7 buffer, protection and enable circuit not established |
| RC12 | JP1: bridged selects 5 V, unbridged 3.3 V | Same unresolved circuit/rating questions |
| RC13 | AUX no longer supports 5-V modification; modification applies to MAIN only | A fixed nominal voltage does not prove bidirectional release |

Source: [Holybro voltage modification guidance](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod).
This identifies revisions with published voltage-selection circuitry, not the
revisions containing a particular buffer IC. **No per-revision buffer part number
or input tolerance is verified.** Obtain a matching schematic/BOM or manufacturer
confirmation before applying a load, bias or generator signal. Do not solder or
change JP/pad state as part of this procedure.

## 2. Connector and preparation

Use **FMU PWM OUT / AUX**, not I/O PWM OUT / MAIN. The
[official pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
assigns **pin 8 = AUX7 signal**, **pin 10 = GND**, pin 9 = AUX8 and pin 1 =
VDD_Servo. In the same keyed connector view as Holybro's pin-order illustration,
pin 1 is rightmost:

```text
left   10:GND  9:AUX8  8:AUX7  7  6  5  4  3  2  1:VDD_Servo   right
```

Cable/wire-entry views may be mirrored. Match the keyed housing and pin-1
conductor before counting; black wire does not mean GND. Map a disconnected
adapter separately. Do not put scope ground on a signal or servo-power pin.

Before power: disconnect **all motors**, ESC signal/BEC leads, servo power and
actuator connections. No propeller/EDF rotor is installed. Leave AUX7 completely
unconnected except approved high-impedance measurement probes. Check instrument
earth/USB grounding and the controller-only power plan. Do not defeat protective
earth. Use supported, current-limited bench power; do not join unreviewed supplies.

## 3. Short operator checklist — stop at the first unmet gate

1. **Unpowered identification.** Remove every power source. Record controller
   model, serial and PCB revision. A competent operator may inspect the unpowered
   case/PCB under the applicable service/ESD procedure; photograph revision and
   selector state without soldering. Do not use board-signal resistance/diode
   measurements before ratings are known: meters inject test current too.
2. **Passively observe the board alone.** Verify connector orientation; observe
   AUX7 versus pin 10 during boot with `SPKT_ENABLE=0`. A multimeter may record
   steady DC, but **an oscilloscope is required** for startup transitions,
   overshoot, excursions and rise/fall behavior. No ESC, bias resistor or generator.
3. **Review Gate A.** Record the actual PA0-to-AUX7 circuit, buffer direction/OE,
   protection, power-state limits and allowable voltage/current/load. If unknown,
   **STOP before steps 5–8**. Passive board-only observations cannot grant input
   tolerance or bidirectional approval. No generic resistor/current limit is
   prescribed for an unidentified circuit.
4. **Receive-only bench observation.** Any firmware loading is a separate manual
   operator action, never automatic. With actuators and AUX7 stimuli disconnected,
   select the reviewed custom image; set `SPKT_ENABLE=1`, `SPKT_TLM=0`,
   `SPKT_EXP=703`, and enable normal disarmed logging (`LOG_DISARMED=1`) if required.
   Reboot. Check the SPKT bench pre-arm warning, unarmed state and SPKH records.
   TE must remain clear, HDSEL set, and AF8/open-drain reported. Scope the idle
   line again. No physical TX is available. This step may be observed board-only;
   it does not authorize the later loading/injection tests if Gate A is incomplete.
5. **Qualified release test, Gate A approval required.** Apply only the load/bias
   method and limits written by the electrical reviewer. Use a scope and approved
   current/impedance method to compare GPIO-input and RX-only release. A high
   voltage or a multimeter reading alone does **not** prove high impedance. Do
   not deliberately connect opposing drivers. Stop for an unexplained strong
   driver, clamp, excess current or voltage outside reviewed limits.
6. **Qualified receive-path test, no ESC.** After input tolerance/path approval,
   use an approved generator at 115200, 8N1. Start with a known four-byte pattern
   at widely spaced intervals, e.g. `55 AA 00 FF` every 100 ms, within reviewed
   electrical limits. Scope the actual signal; compare SPKT Bytes deltas and
   SPKH framing/noise/parity/overrun/overflow deltas. Non-SRXL2 patterns may cause
   protocol rejection; they must not cause hardware UART errors. Record the
   actual generator count. MCU-side probing is optional only at an identified,
   safe test point; never probe unidentified fine-pitch pads. Do not infer real
   ESC discovery from a generator fixture.
7. **Review Gate B.** Confirm measured return propagation, release/rise-time
   behavior, power-state behavior and clean reception. Preserve AUX8: compare
   its existing waveform/configuration without reconnecting actuators or assigning
   a new motor function. Any new output stimulus needs separate authorization.
8. **STOP before physical TX/ESC connection.** Submit the evidence table/captures
   for approval. The first TX qualification is a separately reviewed, bounded
   test into an approved load **without an ESC**. Only after that and the ESC-side
   review can the motorless protocol experiments in section 6 be considered.

On completion or any failure: remove external stimulus/power safely, save logs,
set `SPKT_ENABLE=0` and reboot before normal use. The bench arming/stop latch
does not clear merely by changing ENABLE. Restore any manually changed logging
settings from the saved parameter file. No fallback to active PWM on AUX7.

## 4. Measurement record and GO/NO-GO criteria

Record PCB/selector photographs, APJ SHA-256, full parameters, power arrangement,
instrument/probe settings, raw scope capture, SPKT/SPKH log, reviewer and dated
authorization for each experiment. EXP is only a log label; it authorizes nothing.
Counters are cumulative per boot: use before/after deltas, not isolated values.

| EXP | Measurement | Required equipment | Actual value / evidence | Decision |
| --- | --- | --- | --- | --- |
| 701 | Revision, selector state, matched AUX7 circuit/ratings | Visual inspection and applicable electrical documents | NOT_RUN / unknown | UNVERIFIED |
| 702 | Board-only disabled idle and boot min/max | DMM for DC; scope for transients | NOT_RUN | UNVERIFIED |
| 703 | RX-only idle, TE/HDSEL/mux status and arming inhibition | Scope + GCS/DataFlash log | NOT_RUN | UNVERIFIED |
| 704 | Release, load response, rise time and buffer direction | Scope + reviewer-approved method, after Gate A | NOT_RUN | UNVERIFIED |
| 705 | Actual UART input count / hardware error deltas | Scope + approved generator, after Gate A | NOT_RUN | UNVERIFIED |
| 706 | AUX8 independence / power-state behavior | Scope, approved configuration | NOT_RUN | UNVERIFIED |
| 707 | One-shot TX into a qualified load; TC-to-RX timing | Scope + separate authorized TX image, no ESC | NOT_RUN | BLOCKED |
| 708 | Motorless ESC handshake / later separately qualified telemetry | Scope + qualified connection and protocol authorization | NOT_RUN | BLOCKED |

**Gate A — permission for electrical stimulation:** exact revision/circuit and
weakest-part limits identified, connector/ground/power plan verified, reviewer
has specified safe stimulus/loading and abort limits. Otherwise UNVERIFIED/STOP.
Published 3.3-V nominal logic and MCU FT labels alone do not pass this gate.

**Gate B — permission to prepare a TX qualification:** measured release and
return path work within those limits; no unidentified unidirectional buffer or
unsafe clamp/back-power path; expected byte counts and zero hardware error
deltas in the specified receive experiment. Floating idle, unexplained 5-V
levels, excess drive, lost input or waveform distortion are NO-GO pending review.
An unidentified or unmeasured item remains UNVERIFIED, never an assumed pass.

**Gate C — permission for a motorless ESC protocol test:** separate manual approval
for the specific image, board and ESC; qualified ESC-side voltage/power behavior;
measured one-shot UART TX/release/receive timing into a load; reviewed isolated
test software and packet budget. Scope must show release and receiver readiness
before the first permitted ESC response with an agreed margin. Use actual SRXL2
timing evidence; do not copy DShot's turnaround or treat the software's 174-us
idle guard as an ESC response-time guarantee.

A 14-byte 115200/8N1 frame nominally occupies about 1.215 ms (8.68 us/bit).
Measure actual baud, final stop bit, TC handling, line release and receive start.
The nominal 3-ms software watchdog has timer/IRQ scheduling latency; it is not
a measured electrical deadline. A logic analyzer can help decode qualified UART
levels, but cannot replace scope measurements of impedance, overshoot or current.

## 5. UART4 audit and safe software corrections

The existing Phase 6 bench mode is retained: disabled by default, receive-only,
no motor/channel commands, normal/forced-arming veto and SRV motor-stop latch.
It is not a physical isolation switch; keep motor/actuator wiring disconnected.
`SPKT_EXP` labels captures, while `SPKT`/`SPKH` expose bytes, UART state and faults.
`RelErr` remains a software configuration fault count, not a measured bus-release
or electrical collision detector. No active TX mode is installed or exposed.

| Audited path | Assessment / Phase 7 action |
| --- | --- |
| Initialization | PA0 input until UART4 is reset/configured; HDSEL, FIFO, 115200, RE; TE never enabled by native RX startup. No ordinary `sdStart` TX transient. |
| Pad mode | AF8 open-drain, no invented pull resistor. RX guard now also rejects unexpected SWAP; AUX7 stays the selected unswapped pad. PCB release remains unverified. |
| TX ownership | Prepared port requires exclusive receiver lease and zero unread RX, idle-high/not BUSY, diagnostic whitelist and compile-time hardware gate. Gate remains zero. |
| TC handling | Found and fixed a short-circuit: fault/short completion previously could skip release and leave TCIE asserted. Release is now attempted unconditionally before success/fault is latched. |
| RX turnaround | Found and fixed a second-release bug: the TC ISR releases RX before the vehicle task acknowledges completion. A subsequent release used to flush an early ESC response. Already-valid RX release is now idempotent and preserves FIFO/ring data. |
| Readback/faults | Prepared release checks UE/RE with TE/TCIE clear, HDSEL and SWAP=0; invalid configuration leaves PA0 input. Prepared service also rejects unexpected SWAP. |
| Timeout | Existing virtual-timer abort releases the line even if main-thread service stalls; host regression covers callback cleanup, not MCU timer latency. |
| Collision protection | Idle/ownership/error checks prevent known overlap; UART errors cannot reliably detect analog contention during HDSEL TX. No such guarantee is made. |
| Runtime integration | Physical port/session still not instantiated. Gate-enabled IRQ wrapper, high-resolution completion/acquisition timebase and frame-pump/drain order require reviewed integration before a TX image exists. Existing millisecond RX timestamps do not certify sub-millisecond turnaround. |

Code changes are confined to `SpektrumUART4.{h,cpp}` and the AUX7 configuration
guard in `DiagnosticRX.cpp`, plus new host-only register fixtures. The source
test hook selects a fake HAL only under `SPKTSMART_UART4_REGISTER_TEST`; normal
firmware uses real H743/ChibiOS headers. Eight regression cases execute the actual
port method bodies against that model: closed gate, repeated RX release, normal
TC, faulted TC, short TC, watchdog abort, invalid config and SWAP fault detection.
The model does not simulate voltages, real UART timing, FIFO silicon semantics
or board buffers. It is not a substitute for any unchecked measurement above.

## 6. Future isolated handshake-and-telemetry contract — not activated

No Gate A/B/C evidence or manual authorization exists yet. The shortest next
action is the unpowered identity/circuit check, not another firmware flag.
After evidence is supplied and approved, prepare a **separate**, reviewable
one-shot runner retaining the bench arming/stop latch, default-off physical gate
and exclusive UART4 lease. Manual authorization is bound to the reviewed image,
board, power plan and experiment, expires on reboot/fault, and is not a saved
"TX enabled" parameter. Do not blindly wire the mock auto-discovery scheduler.

| Separately authorized stage | Allowed packets / budget | Required observation / stop rule |
| --- | --- | --- |
| C0: motorless passive ESC observation | No TX; listen at 115200 with a reviewed power plan | Motor completely disconnected, not just prop removed. Capture ESC startup/announcement and signal voltage. No valid announcement/independent protocol evidence means stop for protocol review, not automatic probing. |
| C1: one handshake query | At most one CRC-valid 14-byte type 0x21, controller 0x30 to expected ESC 0x40, assigned unique UID, baud support 0 | Capture transmitted bytes and released line, then listen for a matching valid response with a bounded 50-ms diagnostic deadline. No retry, broadcast, channel data, bind/programming or PWM fallback. Wrong ID/CRC/reset/no reply stops the session. |
| C2: discovery completion | At most one separately approved type 0x21 final broadcast, destination 0xFF, 115200 agreement | Only after C1 shows the expected bus behavior. UID/capabilities do not identify every Avian firmware or prove throttle/reverse compatibility. |
| C3: one telemetry request | At most one documented/independently verified **non-motor** poll for this Avian firmware | Empty-mask type 0xCD is not presumed harmless: existing zero-mask mock fixtures are not physical authorization. If a safe poll is not established, stop after handshake; do not supply guessed neutral/motor channels. Accept one CRC-valid expected sensor response in a reviewed window, then stop TX. |

Record exact SPMXAE1130A unit/firmware identity where available (unknown stays
unknown), packet captures, acquisition/completion timestamps and error deltas.
The current conservative 5-ms telemetry filter is a diagnostic policy, not a
verified Avian deadline. Only fresh, documented fields may reach AP_ESC_Telem;
no RPM without pole count, no guessed consumed capacity/fault values. No throttle,
reverse, configuration write or motor connection is part of these experiments.

The eventual architecture remains **native AUX7 signal plus common ground,
without an installed converter/receiver**. Bench instruments and a qualified
test load do not constitute additional flight hardware. Whether this board can
meet that goal remains an electrical measurement question.

## 7. Actual software evidence and artifacts

| Check | Actual result |
| --- | --- |
| New real-port/register-model regressions, ASan + UBSan, warnings as errors | 8 cases PASS; no reported sanitizer findings |
| Target build / link, custom Plane | PASS; zero compiler warnings |
| Target build / link, custom Copter | PASS; zero compiler warnings |
| Both resulting ELFs | RX-only write returns zero; bench arming veto present; physical TX port/session/commands absent |
| Hardware gate / target object | Gate remains zero; acquire returns false without peripheral writes/calls |
| Physical measurement / ESC communication | NOT_RUN; no equipment connected or hardware flashed |

The host model is guarded against compilation for ARM/Thumb flight-controller
targets. Tests use actual `SpektrumUART4.cpp` method bodies with fake registers,
a fake RX owner and lock/timer operations, not real ChibiOS interrupt execution.
They cover ordering/cleanup regressions; actual bit behavior, pin impedance,
interrupt latency, electrical collision and successful Avian communication remain
unverified. The gate-enabled vector wrapper is still not activated or executed.

| Custom vehicle | Flash used | Free flash | Static Data+BSS |
| --- | ---: | ---: | ---: |
| Plane | 1,591,920 B | 374,152 B | 145,868 B |
| Copter | 1,595,708 B | 370,364 B | 146,960 B |

Static RAM is not runtime peak heap/stack. Validation uses the existing official
ArduPilot Docker/WSL2 build image and supported waf workflow. Final container
`spktsmart-phase7-validation` exited zero. Only the affected custom targets and
new register-model cases were run. Standard images, earlier GTest/protocol
matrices and resource checks were not rebuilt/repeated; their Phase 6 evidence
is retained and is not relabeled as Phase 7 testing.

Full logs: workspace `logs/spktsmart-phase7-linux/`. Reproduction helper:
`tools/validate-spktsmart-phase7.sh`. The collector command is
`python tools/collect_spktsmart_artifacts.py --phase 7 --board Pixhawk6C-SpktSmart`;
it checks board ID 56, decoded APJ length, BIN equality and records source/artifact
SHA-256 hashes. Bench-preparation artifacts are in workspace
`custom-firmware/Pixhawk6C-SpktSmart-phase7-artifacts/`:
`arduplane.apj`, `arducopter.apj`, matching BIN/ELF files and manifest.
No previous artifacts are overwritten.

**Stop here for physical operations:** the next operator task is identification
and passive observation, not enabling TX. Supply the completed revision/circuit
and measurement evidence before a separate TX or ESC-connection review. Neither
this document nor a passing register test grants electrical approval or flight
readiness.
