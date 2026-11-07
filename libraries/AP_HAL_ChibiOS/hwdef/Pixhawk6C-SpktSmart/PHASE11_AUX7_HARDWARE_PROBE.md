# Phase 11: AUX7 probe without opening Pixhawk 6C

2026-11-02, Europe/Amsterdam. AI-assisted changes on the existing feature branch,
base dbe792162d06cab66c3475fd5556bf7a120f119e plus local project changes.
No enclosure opened, soldering performed, controller flashed or physical test run
by the agent. No motor, ESC, handshake or reverse command is enabled.

## Decision and limits

**IMPLEMENTED / TESTED / BUILD VERIFIED:** one diagnostic image per vehicle,
with MCU-input-only probe modes, weak internal pulls and the existing UART RX.
**HARDWARE UNVERIFIED:** actual AUX7 electrical response, buffer, power-state
ratings, impedance and receive path. **NO-GO:** ESC signal connection, external
bias/generator injection and physical handshake transmission at this time.

A disconnected USB-only measurement session can use the procedure below without
opening the case. This firmware cannot guarantee that an UNKNOWN external buffer
will remain high impedance: changing an internal pull could change a buffer's
input and cause its output to drive the connector at its board-selected voltage.
The software guarantee is no MCU GPIO output mode, no UART TE, no PWM/DShot or
protocol transmission on PA0. Weak bias is an intentional electrical stimulus,
not a measurement proving absence of all external drive. Keep every external
load, ESC and actuator disconnected while characterizing this effect.

User-reported measurements, NOT independently verified:

| Measurement | Reported value | Established conclusion |
| --- | --- | --- |
| Isolated ESC signal versus ESC GND | approximately 3.0 V DC | Only a steady DC observation; no transient or drive-impedance evidence |
| AUX7 versus Pixhawk GND | approximately 0.1 V DC | Near-low connector voltage; does not identify floating vs driven/buffered low |
| UART messages | U1/RX1, B0/F0, FE1 | Software RX initialized; one historical framing indication; no consumed bytes or CRC-valid frames |
| BEC setting | 6 V | Separate power domain; NEVER connect BEC positive to AUX7 or the USB-powered servo rail |

## Exact parameters and mode semantics

Use the custom Plane image first. Parameters retain their original IDs/defaults:
SPKT_ENABLE (index 1), TLM (2), EXP (3), SHADOW (4). New SPKT_PROBE is index 5,
default 0, values 0..4. No parameter index was renumbered. The ENABLE parameter's
UI group-hiding flag was removed, so PROBE and independent SHADOW remain visible
when ENABLE=0. That flag controlled parameter enumeration, not arming/safety;
all checks and the legacy ENABLE behavior remain in code and were retested.

| SPKT_PROBE | Name | MCU operation |
| ---: | --- | --- |
| 0 | DISABLED | Probe off; PA0 floating input after probe shutdown. With ENABLE=0 this is the normal protected configuration. ENABLE=1 independently retains the old Phase 10 RX-only path. |
| 1 | GPIO_INPUT_FLOATING | PA0 MODER=input, no internal pulls; UART clock/receiver off |
| 2 | GPIO_INPUT_PULLUP | PA0 input with weak internal VDD pull-up; NO output driver or voltage-selector switching |
| 3 | GPIO_INPUT_PULLDOWN | PA0 input with weak internal ground pull-down; NO output driver |
| 4 | UART_RX_ONLY | Existing UART4, PA0 AF8 open-drain, HDSEL, UE/RE/FIFO; TE clear, no added pull |

Mandatory probe setup: ENABLE=0, TLM=0, SHADOW=0, unarmed. Simultaneous nonzero
ENABLE and PROBE is invalid and stops the probe to floating input. Invalid PROBE,
configuration-readback failure or unexpected arming stops to safe input, records
fault evidence and revokes authorization; arbitrary mode changes while armed are
rejected. Safety shutdown to input is allowed, rather than preserving an active
bias after an unexpected arm state.

Probe authorization is volatile. At boot a saved nonzero PROBE request does NOT
start it. Write PROBE=0 and allow at least one second for the diagnostic task to
observe it, then select 1..4. A normal boot with default 0 already observes zero.
A session expires after 60000 ms of diagnostic-task updates, measured from the
first active request. Switching nonzero modes cannot extend it. After timeout or
fault select 0 before another explicit request. No strong-output pulse is used.
This timeout is not a claim of hardware-autonomous timing if the CPU stalls.

Any nonzero ENABLE/PROBE request, even invalid or ignored at boot, inhibits
arming immediately and latches bench protection once observed by init/update.
The existing emergency-stop latch suppresses motor functions while disarmed.
PROBE=0 alone cannot clear the latch: clear ENABLE and PROBE, then reboot. Never
weaken arming checks, force arm or run servo/motor tests for these measurements.

## Mission Planner and connector procedure

Use [Holybro's pinout and orientation](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports):
FMU PWM OUT is AUX, not I/O PWM OUT/MAIN. Pin 8=AUX7, pin 10=GND, pin 9=AUX8,
pin 1=servo power. Identify keyed housing and pin 1 using the published right-side
view. A cable/wire-entry view can be mirrored. Do not infer GND from wire color.

1. Remove all power. Remove propeller/EDF rotor; disconnect and individually
   insulate motor leads. Disconnect ESC signal, BEC positive AND ground, flight
   battery/servo power, actuators and all output plugs. Do not open or solder the
   FC. Record exterior model/serial labels; vendor lookup may establish revision
   without disassembly. Retain the existing parameter backup and rollback APJ as
   described in PREFLASH_PHASE9.md, using this Phase 11 artifact/hash instead.
2. Establish the measurement ground/USB/earth plan before powering. Use rated,
   high-impedance voltage probes. No resistor, supply, signal generator, logic
   analyzer output, ESC lead or other stimulus is attached to AUX7. Do not use
   resistance/diode measurement on the board signal: these inject current.
3. Any firmware loading is a separate manual operator action. This agent has not
   flashed hardware. Ordinary ArduPilot startup may synchronize unchanged bundled
   IO firmware if needed. After loading, power FC only by USB; keep it unarmed,
   safety engaged and all actuators/ESC disconnected. Confirm actual board/model
   identity and the selected custom vehicle. Do not bypass loader board checks.
4. Read back SPKT_ENABLE=0, SPKT_TLM=0, SPKT_SHADOW=0, SPKT_PROBE=0; saved settings
   can override defaults. Enable suitable microSD/file logging and LOG_DISARMED=1,
   recording the old logging values first. Reboot once into this baseline. Use
   Mission Planner Full Parameter Tree/List; refresh parameters if PROBE is not
   visible. Do NOT enable legacy RX simply to expose probe settings.
5. Start a voltage-only baseline at pin 8 versus pin 10. Record the DMM DC value
   and scope startup/idle waveform using the verified connector orientation. A
   DMM can miss transitions, overshoot, switching or a weakly held level. If the
   measuring instrument/ground arrangement is not reviewed, stop before probing.
6. Set EXP=1101 and PROBE=1. Wait for `SPKP M1 Q1 ...` (five-second reporting),
   record current GPIO level and the external voltage/waveform for at least 10 s.
   Stop the test at PROBE=0. Allow at least 1 s before selecting another mode;
   a final M0/Q0 confirmation is emitted at the next bounded message interval.
7. With the ESC and EVERY external stimulus still disconnected, set EXP=1102 and
   PROBE=2 for the weak internal pull-up observation. Record external voltage and
   MCU readback together; do not assume the connector must reach 3.3 V. Observe
   only within the session's 60 s allowance. Return to PROBE=0, wait at least 1 s.
8. Repeat for EXP=1103/PROBE=3 (weak pull-down), then EXP=1104/PROBE=4 (UART only).
   Return to 0 between experiments. Mode 4 adds no pull; no ESC traffic is
   expected with the lead disconnected. A low/floating input can produce noise
   or FE increments; do not use FE=0 alone as a passing electrical test.
9. Stop on config error G/Err increasing, unexpected mode/arming, reset loops,
   unexplained instability, voltage near 5/6 V, negative excursions or any result
   outside the reviewed nominal signal envelope. Remove USB if safe-input
   readback cannot be trusted. Never attach the ESC to try to force a result.
10. Download the DataFlash log and save scope traces plus settings/instrument
    details for each EXP. Clear ENABLE/PROBE/TLM/SHADOW, restore temporary logging
    values and reboot to leave the bench latch. Keep actuators disconnected.

This sequence does not require opening the case. Its voltage observations narrow
hypotheses; they cannot by themselves certify the return path or physical TX.

## Measurement table and interpretation

All new physical rows remain NOT_RUN. Record actual measured values in the
companion PHASE11_MEASUREMENTS.csv; do not replace them with expected examples.

| Mode | Expected MCU observation | External measurement | What it can establish | What it cannot establish |
| --- | --- | --- | --- | --- |
| 0, ENABLE=0 | Input/no pull, UART off | DC baseline and scope boot/idle | Connector voltage in protected idle configuration | Floating vs actively driven low, input tolerance or RX path |
| 1 | Mode 1, UART U0; level may be 0/1/noisy | DC and waveform with no added bias | Relationship between sampled MCU level and connector behavior | Low means active pull-down, or high means safe weak idle |
| 2 | Mode 2, UART U0; input often high if lightly loaded | DC and transitions into/out of weak internal pull-up | Whether internal bias changes MCU readback and/or connector | External high impedance, connector output type, external-to-MCU reception or safe ESC connection |
| 3 | Mode 3, UART U0; input often low if lightly loaded | DC and release waveform | Response to weak internal pull-down | Ability to overcome a strong external high, input rating or bidirectional buffer direction |
| 4 | Mode 4, U1; AF8/HDSEL, TE=0 | Idle scope trace; no UART TX waveform should be generated | RX initialization and current internal sampling; future approved RX bytes | Any UART TX timing/turnaround or active ESC compatibility |

If MCU readback changes with the pulls but pin8 remains around 0.1 V, the path is
not demonstrably transparent: buffering, isolation, other bias, mapping or wiring
must be investigated. If both follow, the observations are consistent with a
connected path but an output buffer can also follow its input in that direction.
This is NOT proof that an externally driven pin8 signal can reach PA0. If neither
moves under pull-up, persistent loading/clamping/drive is one possible cause;
settings and GPIO errors must also be checked. Do not diagnose an active low
solely from 0.1 V, a logic-zero flag, or unsuccessful weak pull-up.

Distinguishing floating from actively driven low definitively requires impedance
or controlled bidirectional stimulus evidence. **An external pull-up/probe is
NOT currently approved or sufficiently specified.** The unknown circuit, output
state, powered/unpowered ratings and allowable injection current prevent deriving
an approved resistor value, supply source or safe maximum current. Merely dividing
3.3 V by a chosen resistor does not establish safety or backfeed tolerance. No
external 5 V signal is permitted. A later, separately reviewed low-voltage,
current-limited test can be designed once these ratings are established through
manufacturer information or a qualified non-invasive circuit assessment. Do not
use a random connector's power pin as an assumed verified 3.3 V source.

## Revisions and why one image is used

[Holybro](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod)
describes RC09 voltage-selection pads and RC12 JP1 (bridged selects 5 V). RC13
modification applies to MAIN only, not AUX. Earlier revisions and this unit's
selector/buffer remain unknown. The firmware cannot switch solder pads or infer
their state. This one image changes only the H743 PA0 GPIO input/pull or UART RX;
no board voltage or buffer-direction command is invented. Separate builds cannot
repair missing physical return-path evidence or change jumpers. A buffer may
amplify a weak internal transition to a different connector voltage, so neither
revision nor compatibility is inferred from internal register success. If vendor
information identifies a revision needing different electrical controls, that
requires a separately reviewed implementation, not blind mode selection.

## Diagnostics: MCU-only, not connector proof

Mission Planner every 5 s while a probe is requested/active, including a bounded
final disabled confirmation:

- `SPKP M2 Q2 L1 U0 G0`: M applied probe mode, Q requested parameter, L sampled
  MCU level, U UART initialized, G cumulative software configuration/release
  errors (includes GPIO checks and UART ownership/configuration faults).
- `SPKP B0 F0 FE1; MCU only`: B consumed RX bytes, F CRC-valid framed packets,
  FE cumulative UART framing indications. B/F/FE retain history across modes;
  they are not freshly received data while GPIO mode has U0.

M0/Q2 can mean a saved request is waiting for zero-first authorization, a timeout,
conflict or fault. Do not mistake Q2 for an active pull-up. Select 0, wait, then
explicitly request the reviewed mode; investigate G/Err and logs if it fails.

At 1 Hz, SPKP fields: TimeUS, Exp, Req, Mode, Level, UART, Edges, Seen, Dur, Err,
Ext. Edges counts changes of the MCU digital input sampled at the existing 1 kHz
timer; Seen bit0=low observed, bit1=high observed; Dur is current mode sample
milliseconds. Sampling/counters restart on applied mode changes, and retain the
last values after shutdown. It misses faster transitions and is not UART edge
capture or an analog measurement. Err is the same configuration-fault counter.
**Ext is always 0: firmware has no evidence proving external pin response.**
SPKQ records TimeUS/Bytes/Frames/FrameErr. Existing SPKH/SPKT/SPKR remain in UART
mode, and SPKS Plane shadow logging is unchanged. Mode changes discard buffered
RX and reset parser observation so GPIO noise is not converted into fresh data.
No synthetic data is reported as physical ESC measurements.

## Firmware implementation and validation

Changed firmware files:

- libraries/AP_HAL/UARTDriver.h: optional receive diagnostic sample fields; no
  new virtual API or SerialManager port. Existing set_options is reused only by
  the dedicated stopped RX backend for input/pull selection.
- libraries/AP_HAL_ChibiOS/DiagnosticRX.h/.cpp: PA0 input transitions under system
  lock, UART disconnect/off before pulls, readback/ownership/armed guards,
  sampling and fault shutdown; default RX framing/baud and zero-write stub intact.
- libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Probe.h: volatile zero-first,
  disarmed/conflict/deadline policy without any HAL writes.
- AP_SpektrumSmartESC_Diagnostics.h/.cpp: PROBE parameter and arbiter for mutually
  exclusive GPIO versus existing receive mode, arming latch and SPKP/SPKQ logs.
- tests/test_spektrum_bench.cpp: policy, actual arming, metadata visibility and
  real SRV aileron/elevator preservation tests.
- libraries/AP_HAL_ChibiOS/tests/DiagnosticRX_test_hal.h and
  test_diagnostic_rx_registers.cpp: host register/OS model compiling the actual
  DiagnosticRX class/methods. It does not model electrical buffers or MCU timing.

Outer tools: validate-spktsmart-phase11.sh, finalize-spktsmart-phase11.sh,
inspect_spktsmart_phase11.py; artifact collector and prior audit helper extended
for Phase 11. No hwdef mapping, IO image, throttle/reverse generator, Plane/Copter
motor path or source number domain changed. No actual digital output arbiter added.

Build environment: existing Linux Docker image ardupilot/ardupilot-dev-chibios:v0.1.3
(pinned digest in manifest), GCC Arm 10.2.1. Four builds passed, zero compiler
warnings, including stock regression images for the shared diagnostics structure.
Final reporting/metadata corrections affected custom diagnostics only; those two
images and affected bench tests were rebuilt, without repeating stock builds.

| Target | Vehicle | Flash bytes | Free flash bytes | Static RAM bytes | Warnings |
| --- | --- | ---: | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | plane | 1594460 | 371608 | 145912 | 0 |
| Pixhawk6C-SpktSmart | copter | 1597712 | 368356 | 147004 | 0 |
| Pixhawk6C | plane | 1586660 | 379408 | 141688 | 0 |
| Pixhawk6C | copter | 1590444 | 375628 | 142776 | 0 |

Final GTests: protocol 32, mock protocol/handshake 31, control/shadow 19, receive 8;
feature-enabled bench 14 and receive 8. Total 112 passing executions across both
feature configurations. Sanitizers: protocol 31 + mock 31 + control 19 + probe
policy 10 + actual DiagnosticRX register-model 4 = 95 ASan/UBSan cases passed.
18 configuration/AUX7-candidate/bdshot resource checks passed; candidate generation
is not installation or electrical proof. No new UART TX path was created.

Register tests verify all three input/pull modes, UART transition and stop,
readback failure, armed rejection, owner conflict, FE recovery, unexpected TE
shutdown, transition sampling and unchanged adjacent GPIO fields (including PA1).
The PAL model asserts no GPIO output-mode attempt and no TDR write. Native
inspection retains hardware gate 0, no live handshake/motor engine, zero RX-write
stub and arming veto, and confirms new probe log strings in both custom images.
Physical AUX8 PWM timing remains unmeasured, despite unchanged mapping/source.

Actual SRV tests confirm emergency-stop does not change aileron/elevator PWM,
while preserving existing motor suppression. A metadata flag test prevents PROBE
being hidden when legacy ENABLE=0. Source inspection and policy tests cover
safe startup/reset; no physical reboot or electrical receive test is claimed.
Earlier Phase 9 live disarmed shadow evidence is retained; no unchanged flight
simulation was rerun solely to claim extra test coverage.

Artifacts and hashes (custom board ID 56), with APJ/BIN equality and source manifest:

- `custom-firmware/Pixhawk6C-SpktSmart-phase11-artifacts/arduplane.apj`
  SHA-256 `3bf750f4e267b34f9dc2ffaf055de5b46721a51e4dd8a9f1efad064bcb322618`

- `custom-firmware/Pixhawk6C-SpktSmart-phase11-artifacts/arducopter.apj`
  SHA-256 `114beba96661dd3da7e1fcb278c78110351da705a5dc0b6dff29c9e871508ac0`

Full logs: logs/spktsmart-phase11-linux. SHA256SUMS and artifacts-manifest.json
are saved alongside images. No automatic flash. All project source hashes and
saved ELF/payload provenance are checked by the final artifact audit.

## Subsequent ESC and handshake experiments: still separate gates

Do NOT connect the measured 3.0 V ESC signal merely because a pull makes AUX7
rise. First establish connector input tolerance, safe powered/unpowered states,
real return-path evidence and acceptable signal/bias/ground behavior. Obtain
revision information non-invasively from the vendor if the case must stay closed.
After separate approval, remove all power and use only AUX pin8 to identified
ESC signal, pin10 to ESC ground. Keep BEC positive disconnected and individually
insulated, all motor leads disconnected/insulated, no EDF rotor/propeller, USB FC
power and separately reviewed ESC power/ground sequence. Remain disarmed under
bench protection; start RX-only, not a GPIO pull experiment with an ESC attached.

Use the legacy SPKT_ENABLE=1/PROBE=0 or the separately authorized PROBE=4 path,
never both. Zero bytes may be normal if the ESC waits for master traffic; it does
not diagnose a broken line. The documented SRXL2 idle/master/poll rules and exact
Avian initialization remain as described in PHASE10_REAL_ESC_BENCH.md.

The existing Phase 10 one-shot handshake simulation is retained, not recreated.
It still has a zero compile-time physical gate, no runtime factory/parameter,
no automatic reboot authorization, no motor frames and no polls after completion.
Future TX needs electrical/turnaround review, controller-role validation, explicit
manual approval and a separately reviewed firmware binding/logging the session.
PROBE=4 does not and cannot enable it. Physical throttle/reverse require the real
MAIN3/AUX7 output coordinator and all prior motor safety gates; they are outside
Phase 11. Stop here at the physical measurement boundary.
