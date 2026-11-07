# Phase 10: real ESC bench diagnostics

2026-11-02, Europe/Amsterdam. AI-assisted work, existing feature branch, base
commit dbe792162d06cab66c3475fd5556bf7a120f119e with uncommitted project changes.
No device flashed, wire connected, ESC powered or physical measurement performed.

**USB-only disconnected diagnostic firmware: SOFTWARE/BUILD VERIFIED.**
**Direct AUX7-to-ESC RX connection: NO-GO, ELECTRICAL HARDWARE UNVERIFIED.**
**Physical handshake TX: NO-GO. Throttle/reverse: disabled and not implemented
as physical outputs.** A successful build or timer capture configuration does not
qualify the actual board or ESC electrical circuit.

## 1. What changed

- AP_SpektrumSmartESC.h/.cpp: historical Observation metadata for complete
  CRC-valid frames, accepted parser results, last acquisition timestamp/type,
  sensor type and handshake source (including unexpected IDs). This does not
  loosen discovery, telemetry-context, CRC or freshness rules.
- AP_SpektrumSmartESC_Diagnostics.h/.cpp: two short Mission Planner INFO messages
  every 5 seconds, replacing potentially frequent state-change messages; explicit
  passive-silence explanation; new 1 Hz SPKR DataFlash record. Existing SPKH,
  SPKT, SPKS, RX-only port, parameters, telemetry frontend and arming veto remain.
- AP_SpektrumSmartESC_HandshakeBench.h: offline one-shot wrapper reusing the
  existing Bus/Handshake engines. Volatile explicit reviewed+disarmed approval,
  200 ms total deadline, bounded retries and TX completion checks inherited from
  Bus, release on stop/disarm/fault/unexpected traffic. Successful confirmation
  ends the experiment; no polling or control frames can be requested through it.
  No firmware factory/vehicle caller or parameter activates this wrapper.
- tests/test_spektrum_smart.cpp: two observation cases for CRC rejection,
  unexpected ID, historical-vs-fresh state and unsolicited telemetry rejection.
- tests/test_spektrum_phase4.cpp: four one-shot cases: manual/default/hardware
  gates, successful handshake-only exchange, deadline/wrap/disarm release,
  unexpected frame cancellation. All existing suites retained.
- Outer tools/validate-spktsmart-phase10.sh, inspect_spktsmart_phase10.py:
  builds/tests/resource checks plus linked diagnostic strings and no-TX proof.
- Outer artifact collector supports Phase 10; existing preflash audit helper
  accepts --phase 10 for snapshot and saved ELF/BIN verification. No old artifact
  overwritten. This document and README link are added.

No HAL pin initialization, UART registers, hwdef, IO firmware, servo mapping,
Plane throttle/controller, Copter motor path or shadow observer logic changed.
A physical digital-mode MAIN3/AUX7 arbiter still does NOT exist. The old simulated
Ownership checks must never be presented as real IOMCU inhibit acknowledgements.

## 2. Electrical evidence and limits

The archived official [SRXL2 Rev K specification](https://github.com/SpektrumRC/SRXL2)
(sources/Spektrum-SRXL2/specification.pdf) specifies a 3.3 V signal, idle high,
non-driven idle with weak bias, and 115200 baud default/8N1. Its servo-bus power
range is a separate electrical domain. Its receiver-selected master and
poll-driven communication rules mean a passive ESC-only connection may stay
silent; a flight-controller master is an experimental role, not established
compatibility with this exact ESC. Raw protocol support does not prove Avian
Smart Throttle initialization behavior or reverse calibration.

[Holybro revision guidance](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod)
identifies RC09 voltage-selection pads, RC12 JP1 and RC13's MAIN-only voltage
modification. It does not identify the AUX7 buffer/return-path IC or its ratings.
The [download listing](https://docs.holybro.com/autopilot/pixhawk-6c/download)
currently offers case/adapter CAD, not the required revision-matched schematic.
No new board revision, schematic/BOM, selector photo or scope trace was supplied.
The user's board revision therefore remains UNKNOWN. No pad modification is made.

AUX7 PA0/UART4_TX AF8 supports MCU single-wire HDSEL reception. The existing
receive backend uses RE/UE/FIFO, keeps TE clear and selects open-drain AF8.
Neither setting proves a board buffer is bidirectional. This RX implementation
does not add a pull-up. A floating line can read high or low; an instantaneous
line-high flag does not establish idle voltage, bias or high impedance.

Official bidirectional DShot on PA0 provides relevant software return-path
capture/resource evidence, not measurements of this actual PCB, buffer direction,
level tolerance or asynchronous UART signal quality. Its AF2/timer/DMA encoding
is not SRXL2 AF8 UART. The six UART candidate resource checks generate an
UNINSTALLED configuration with pull-up; they do not modify the running RX image.

The [exact Ver. A product listing](https://www.horizonhobby.com/product/spektrum-avian-130-amp-brushless-smart-esc-ver-a-3s---6s/SPMXAE1130A.html)
notes model-specific programming/lead differences. No verified target-unit
firmware version, signal impedance, signal waveform or boot behavior is available.
User-reported 6 V BEC setting is NOT a measurement of the signal wire. Never
connect BEC positive to PA0/AUX7, or to the USB-powered servo rail in this test.

## 3. Missing measurement record: all NOT_RUN

| Evidence required | Instrument/review | Required conclusion before proceeding |
| --- | --- | --- |
| Actual PCB revision, selector state, signal path | Revision photographs, matching schematic/BOM or manufacturer confirmation | Identify PA0-to-pin8 buffer, OE/direction, protection and power-state ratings |
| Controller-only AUX7 boot/idle waveform | Scope with suitable high-impedance probing | Capture min/max, excursions, idle changes; DMM DC alone cannot qualify input or release |
| Isolated ESC signal waveform, motor absent | Separately reviewed ESC power test, scope, signal-to-ESC-ground only | Establish signal levels, boot transients and whether it drives/requires bias without connecting FC |
| Ground/power plan | Circuit review and supply/instrument ratings | No USB/earth conflict, BEC backfeed or incompatible powered/unpowered state |
| Input tolerance and protection | Actual buffer/MCU datasheets for this circuit | Establish allowed voltage/current/injection and power-off limits; no generic 5-V-tolerance assumption |
| Receive path and direction | After ratings approval, controlled stimulus and scope/logic decode | Evidence that pin8 signal reaches UART4 without another driver or MAIN/AUX cross-coupling |
| High impedance/release/bias | Approved test load/current observation and scope, after ratings review | Establish actual release and allowable bias; no arbitrary resistor/pull-up recommendation |
| TX completion/turnaround/contention | Future separately authorized handshake-only firmware, scope | Full last stop bit, safe release and valid return; current firmware CANNOT perform this experiment |
| AUX8 independence | Scope/logic capture after approved fixture design | No coupling or ownership change; do not enable actuator outputs in the initial test |

A logic analyzer can decode 8N1 and timing only AFTER its input rating and the
analog voltage envelope are established. It cannot prove analog tolerance,
overshoot, high impedance or contention current. A multimeter is useful for
reviewed steady DC/power measurements, not bidirectionality, UART timing or safe
line release. Avoid resistance/diode mode on unidentified board signal paths:
those modes inject current. No external test signal is permitted before ratings
and the circuit are reviewed. No resistor/converter is specified or deemed
necessary yet; native communication remains the preferred but unproven design.

## 4. Practical operator procedure with explicit stop gates

1. **Remove power.** Remove propeller/EDF rotor; disconnect all motor leads and
   individually insulate them. Disconnect flight battery, ESC/BEC, all MAIN/FMU
   output plugs and actuators. Record FC/ESC model/serial/revision and actual ESC
   firmware/settings. Do not recalibrate throttle, alter Brake/Reverse settings,
   update ESC firmware, solder jumpers or attach test generators.
2. **Retain rollback.** Follow PREFLASH_PHASE9.md backup/rollback instructions,
   substituting the Phase 10 APJ/hash below. Save the current parameter file and
   known-working Pixhawk6C Plane firmware. Do not bypass board-ID checks. Firmware
   loading is a separate manual operator action; the agent has not performed it.
   Ordinary startup can synchronize unchanged bundled IO firmware if installed IO
   differs. Keep every actuator/ESC disconnected throughout.
3. **USB-only baseline, no signal connection.** Confirm actual board identity and
   use only USB power. Leave ESC unpowered. Start with SPKT_ENABLE=0, SPKT_TLM=0,
   SPKT_SHADOW=0. A manual flash may then be used for this disconnected test.
   Verify unarmed status; do not arm, release safety or invoke servo/motor tests.
4. **Enable protected passive diagnostics.** With outputs still disconnected set
   SPKT_ENABLE=1, SPKT_TLM=0, SPKT_SHADOW=0 and a new SPKT_EXP label (e.g. 1001).
   Enable LOG_DISARMED=1 and an existing file backend/microSD as needed, retaining
   previous logging values. Reboot. Confirm the RX-only/arming-inhibited notice.
   Normal/forced arming is vetoed and the bench emergency-stop latch is set.
   Do not attempt to defeat the veto. No MAIN3 wire goes to the ESC.
5. **Record the no-ESC baseline.** In Mission Planner Messages, wait at least 10 s
   for the two five-second summaries. Download the DataFlash log. Expected:
   UART initialized (U1), bytes/frames/CRC typically zero, no observed device or
   valid telemetry. Floating input may instead produce noise; preserve logs and
   scope evidence rather than classifying it as ESC traffic. No physical ESC
   detection is claimed by this baseline. Record SPKH and SPKR even if empty.
6. **STOP: electrical evidence gate.** Complete/review the measurement table.
   Power the isolated ESC only after a separate wiring/power review, with motor
   leads still disconnected. Its BEC positive remains isolated from the FC. Do
   not join its signal to AUX7 during this isolated measurement. A missing motor
   may affect undocumented boot behavior; silence cannot prove incompatibility.
7. **Verify connector orientation before any approved signal connection.**
   [Holybro's keyed pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
   is FMU PWM OUT (AUX), pin 8=AUX7, pin 10=GND, pin 9=AUX8, pin 1=servo power.
   Match its right-side pin-1 diagram to the actual keyed housing; wire-entry
   views can be mirrored. MAIN/I/O PWM OUT is a different connector. Do not infer
   ground from wire color or count pins from an unverified cable view.
8. **RX-only connection, ONLY after separate documented approval.** Remove all
   power before wiring. Approved connection is AUX pin8 to identified ESC Smart
   signal and pin10 to identified ESC ground. BEC positive is disconnected and
   individually insulated; no pin1 connection. Review common-ground/scope-earth
   arrangements first. USB powers FC; separately reviewed supply powers ESC only
   in the approved sequence. All motors/actuators remain disconnected. This step
   is currently NO-GO, not authorized by this report.
9. **If step 8 is later approved, capture real evidence.** Keep ENABLE=1/TLM=0,
   use a new EXP, remain disarmed and record power-on scope trace plus serial/log
   capture. Bytes, CRC-valid frames and observed ID/type are the first goals.
   Do not interpret zero packets as a failed receive circuit. Do not attach a
   Spektrum receiver/converter or drive PWM to provoke a response. Telemetry
   publication is a later reviewed experiment, only with valid context/fresh data.
10. **STOP again before handshake TX.** Review raw traces, expected ESC behavior,
    115200/8N1 timing, line release and master-role semantics. Current firmware has
    no parameter that enables handshake transmission. Do not change a generic
    SERIALn protocol or flip the compile gate to make a silent ESC answer.
11. **Shutdown.** Remove ESC power first according to the reviewed plan; keep all
    signals disconnected before further work. Clear SHADOW/TLM/ENABLE, restore
    logging settings and reboot before leaving bench mode. ENABLE=0 alone cannot
    clear its latched arming protection until reboot. Never reconnect actuators
    merely because a diagnostic run ended successfully.

## 5. Parameters, messages and log interpretation

No new user parameter, persistent TX enable or output mapping is introduced.
Existing parameters: SPKT_ENABLE 0/1 (default 0, reboot required); SPKT_TLM 0/1
(default 0); SPKT_SHADOW 0/1 (default 0, Plane read-only); SPKT_EXP nonnegative
experiment label (default 0). Retained saved values override defaults: read them
back after loading. Leave TLM/SHADOW off for initial physical RX experiments.

Every 5 s while receive diagnostics are active:

- `SPKT RX1 ID00 B0 F0`: state 1 means receiver running, ID is the last observed
  CRC-valid handshake source, B received bytes, F CRC-valid framed packets.
- `SPKT U1 L0 C0 FE0 T00 S00`: U initialized, L instantaneous digital input level,
  C CRC failures, FE UART framing errors, T last CRC-valid packet type, S sensor
  type only when the last frame was a complete telemetry envelope.

These examples describe field syntax, not observed hardware results. ID40 is
expected by the existing ESC decoder, not proof of SPMXAE1130A identity. Other IDs
are exposed as historical evidence even if rejected. RX state 0=not running,
1=running, 2=fresh expected device handshake, 3=fresh validated telemetry,
4=telemetry frontend conflict. Freshness is currently 50 ms, so a five-second
summary can miss a brief state 2/3; cumulative counters preserve the evidence.
There is deliberately no claim of authenticated ESC connection or firmware ID.

DataFlash at 1 Hz:

- SPKH: raw CR1/CR3/ISR, backend flags, framing/noise/parity/overrun/overflow,
  release errors and silence episodes. TE must be clear; HDSEL must be set. The
  CPU register/pin flags do not establish external buffer direction or tolerance.
- SPKT: device/UID, bytes/hardware errors, CRC/length/incomplete/rejected/unknown
  counts and accepted ESC handshake/poll/telemetry counts, last byte timestamp.
- NEW SPKR: TimeUS, Exp, Frames (CRC-valid framing), Accepted (parser accepted,
  not authenticated identity), LastMs (acquisition time of last CRC-valid frame),
  Type, Sensor, HSID (last observed handshake source). Historical fields can be
  stale; use timestamps and SPKT freshness before drawing conclusions.
- SPKS remains the independent Plane shadow observer; it generates no packets.

Interpretation: B>0/F=0 may be noise, wrong protocol or incomplete traffic. C or
FE rising may indicate timing/noise/electrical problems, not a diagnosed root
cause. F>0/Accepted=0 proves only framed traffic, not usable ESC data. T=0x21 is
handshake; T=0x80 is telemetry envelope; sensor 0x20 is a documented ESC sensor
format. Unexpected/unsolicited telemetry can be counted without being published.
No ESC telemetry is fabricated; stale/contextless data still fails publication.
The silence timeout counts quiet episodes, not "ESC disconnected" events.
Without an active master, passive silence may be normal even with a healthy ESC.

## 6. Handshake-only preparation and remaining activation gates

The offline one-shot wrapper has no vehicle integration. It reuses documented
0x21 framing/CRC and the existing bus transaction scheduler. Simulated authorization
requires reviewed+disarmed booleans and the Bus electrical/controller/init gates;
an unsimulated port additionally fails the compile-time hardware gate, still 0.
Authorization is RAM-only and absent on construction/reboot. Stop/disarm,
unexpected traffic or a 200000 us deadline releases RX and ends the experiment;
TX TC/timeout/collision handling remains in Bus. Release failure remains FAULT,
not a false assertion of a safely released line.

Mock tests inspect BOTH outgoing frames: query and confirmation are 0x21,
14 bytes and diagnostic-whitelist valid. No 0xCD control/poll, throttle, reverse or
configuration command is requested. Successful confirmation immediately releases
ownership; no automatic retry after session end. Received type/CRC are exercised
by mock observation. There are no real transmitted-frame logs because this build
has no transmitter/session instantiated; real TX counters would remain zero.

A future physical handshake experiment still requires: matching circuit/rating
review, actual waveforms including power transitions, verified safe release and
turnaround plan, an explicit experimental controller-role review for this ESC
firmware, manual approval, and reviewed firmware binding this one-shot session
with TX/RX type logging. No persisted parameter or boot hook may authorize TX.
Those physical gates are not inferred from simulations, receiver-managed SRXL2,
or bidirectional DShot. No automatic polling is added just to break passive silence.

## 7. Build, tests and artifacts

Image ardupilot/ardupilot-dev-chibios:v0.1.3, pinned digest in manifest;
GCC Arm 10.2.1, existing Linux volume. Both custom builds succeeded:

| Target | Flash bytes | Free flash bytes | Static RAM bytes | Compiler warnings |
| --- | ---: | ---: | ---: | ---: |
| Plane | 1592900 | 373168 | 145872 | 0 |
| Copter | 1596184 | 369884 | 146964 | 0 |

GTests executed: protocol 32; mock bus/command/handshake 31; control/shadow 19;
receive 8 (diagnostics disabled); bench/actual arming/SRV 8 and receive 8 again
with diagnostics enabled. Total 106 passing test executions across the two feature
configurations. Sanitizers: protocol 31 + mock 31 + control 19 = 81 passing
ASan/UBSan cases. All builds use the existing test framework; no hardware behavior
is inferred. Six configuration + six AUX7 candidate + six bdshot checks passed
(18). The candidate is generated for resource analysis, not installed.

Linked inspection proves the new SPKR/message strings exist in BOTH images.
Physical TX, Bus/Handshake/Commands/Control engines remain absent in native ELFs;
RX write returns zero, bench arming checks remain, hardware gate=0 and native
acquire returns false without peripheral writes. Saved ELFs equal linked outputs
and reproduce exact BINs using 0xff gap fill. All 49 manifest source files match
the Linux snapshot with LF normalization. No unchanged stock build repeated.

Logs and reproducible commands are in logs/spktsmart-phase10-linux and the
Phase 10 validation/inspection scripts. The manifest includes source and artifact
SHA256, board ID 56 and APJ decompression/BIN equality checks. Prior phase firmware
and audits are retained as historical artifacts, not hashes of today's source.

- plane: `custom-firmware/Pixhawk6C-SpktSmart-phase10-artifacts/arduplane.apj`
  SHA-256 `8150167ae266857a3dc9b2e519b9a0565797e1ee4fa90f8ed87ee0b68371ec20`
- copter: `custom-firmware/Pixhawk6C-SpktSmart-phase10-artifacts/arducopter.apj`
  SHA-256 `706e028c36e8e6efc949db81c77259ca4d673f00457942a224abb35053947fc1`

## 8. Decisions and next physical evidence

- GO for separately chosen manual firmware loading and a disconnected, unarmed
  USB-only diagnostic run, subject to board/backup/operator checks above.
- NO-GO for direct ESC signal connection: revision, buffer/protection/rating,
  bias/release, isolated ESC signal and combined power-state evidence missing.
- NO-GO for any physical handshake or telemetry polling: electrical and
  experimental master-role/ESC-init evidence and explicit approval missing.
- NO-GO for physical digital throttle/reverse: beyond this phase. Requires real
  exclusive MAIN3 inhibition/IO ACK and AUX7 ownership, final vehicle-demand/safety
  integration, target calibration/reverse settings and stop/dwell proof, verified
  loss/disarm/reset behavior and actual fault/telemetry tests. Copter has no generic
  throttle adapter; its existing motor behavior is untouched.

Preserved numbering: Plane SERVO1/aileron, SERVO2/elevator, SERVO3/function70
throttle are independent of virtual SRXL2 1/index0 and 7/index6, and of physical
AUX7/PA0. No assignment is migrated. Conventional MAIN3 remains available outside
bench suppression, so it must NEVER be wired to this diagnostic ESC at the same
time as the proposed AUX7 experiment. Disconnecting/insulating is the first bench
barrier; it is not replaced by a shadow simulation flag or an assumed motor stop.

Stop here until the board-specific measurements and approval record are supplied.
