# Phase 12: gated real SRXL2 handshake reader

2026-11-02, Europe/Amsterdam. AI-assisted work on the existing feature branch,
base dbe792162d06cab66c3475fd5556bf7a120f119e plus local changes. Nothing flashed,
no physical TX activated, no controller/ESC operated by the agent.

## Verdict and actual milestones

**IMPLEMENTED / TESTED / BUILD VERIFIED:** bounded handshake coordinator, native
UART4 transaction entry, manual request/status/logging, safe return to passive RX,
no Control Data transmission. The supplied APJ retains electrical TX gate **0**.
**Not a complete official SRXL2 master implementation:** Spektrum's master-specific
source is not public. No active-TX APJ is supplied, including for electrical tests.

| Milestone | Actual result |
| --- | --- |
| Static AUX7/ESC level tests | USER-REPORTED physical evidence, recorded below |
| CRC-valid physical Avian handshake response | NOT ACHIEVED; reported B0/F0, no physical packet supplied |
| Actual ESC telemetry measurement | NOT ACHIEVED; no physical sensor packet supplied |
| Software/native-driver validation | PASS, as detailed below; never substituted for physical milestones |

Current decision: GO for a separately chosen manual flash and disconnected,
disarmed USB diagnostic test. Physical handshake TX remains **NO-GO** until the
TX voltage/release/contention evidence is reviewed. A normal parameter cannot
override the compile-time gate. Direct ECU compatibility and flight readiness are
not claimed. The operator's existing ESC connection is reported evidence, not a
new connection authorization from this report.

## 1. Evidence carried forward

User reports actual Pixhawk6C/Avian SPMXAE1130A, Plane V4.7.1, AUX7/PA0/UART4,
ESC signal about 3.0 V, AUX7 weak pull-up 3.21..3.30 V, pull-down about 0.15 V,
expected floating behavior, connected RX `M4 Q4 L1 U1 G0`, B0/F0 and no framing
errors. This is positive static-level evidence in both proposed signal directions.
It does not show UART edges, output impedance, released-line behavior, transient
levels, contention current, or a CRC-valid response. BEC=6 V remains a separate
power-domain setting; its positive lead must not go to AUX7 or the USB servo rail.
No scope traces, circuit ratings or digital traffic capture were supplied.

## 2. Official implementation audit: IDs and the missing master code

Sources used rather than guessing from the FC device ID:

- Official [spm_srxl.h](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.h):
  its final SRXL_INCLUDE_MASTER_CODE block says master-specific code is not open
  and includes spm_srxl_master.h. The public package is not a complete master SDK.
- Official [spm_srxl.c](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.c):
  srxlInitBus automatically marks only ID 0x10 master; its handshake handling also
  recognizes FC-master broadcasts. Being FC 0x31 alone is NOT master election.
- Existing MIT-licensed libraries/AP_RCProtocol/spm_srxl.{h,cpp}, config header,
  and archived sources/Spektrum-SRXL2/specification.pdf Rev K. These existing RC
  input sources/licenses remain untouched. GTest compares generated handshake
  layout and CRC against the actual upstream-derived struct and CRC macro.

This phase implements an explicitly requested, isolated FC-master EXPERIMENT
using public handshake packet semantics, not proprietary source or a general
receiver/master stack. Source ID is 0x31 (decimal 49, FC unit 1). It does not
impersonate a radio receiver or advertise RF telemetry. Other masters/unexpected
sources terminate the attempt. The dedicated bus must contain only this FC and
one motor-disconnected ESC. Do not bridge it to an existing receiver SRXL2 bus.

Target is a candidate ESC ID 0x40..0x4F (decimal 64..79); 0x40 is the public default,
not evidence of the actual unit's ID. Each explicit attempt targets ONE candidate;
there is no address scan. Actual device ID/UID/priority/capabilities are recorded
only from a CRC-valid reply with matching source and destination 0x31. A source
in the ESC range identifies an ESC-class device, not a product model or firmware
version. The isolated physical wiring identifies which unit is under test.

Handshake packets are 14 bytes, type 0x21, UID little-endian, XMODEM CRC big-endian.
FC UID is a hash of the real HAL system identifier, not a fabricated ESC UID.
Startup and final selection stay 115200/8N1. Peer baud bit 1 advertises 400k support;
final broadcast selects 115200. The logged selected baud is not an independently
acknowledged peer setting. No baud-change or ESC configuration command is sent.

## 3. Implemented coordinator and hardware path

The new AP_SpektrumSmartESC_Reader coordinates the existing parser/CRC and
SpektrumUART4 backend. It does not replace completed telemetry, shadow, GPIO probe
or old mock Bus/Handshake suites. The old 0x30/retrying/poll-capable mock session
is NOT activated for this experiment. The new transaction is deliberately narrower:

1. Consume one manual request, validate disarmed bench context and electrical gate.
2. Listen 50 ms. Wait for conservative 2 ms RX silence before sending; this exceeds
   two 115200-baud character times with the acquisition timestamp resolution.
3. Send one query to the selected ESC candidate. Never retransmit it on timeout.
4. Acknowledge transmission only on native UART TC, using its captured completion
   timestamp, not queued bytes or main-loop acknowledgement time. Keep earlier
   peer bytes buffered until TC is acknowledged by the coordinator.
5. Accept a matching CRC-valid handshake within 50 ms; then one final broadcast,
   selecting 115200. That confirmation is part of the same attempt, not a retry.
6. End on completion, timeout, invalid context or unexpected traffic and restore
   RX-only. Maximum logical session 200 ms; reserve the native 3 ms watchdog
   window rather than scheduling a new frame at the end. No restart after ending.

The native entry whitelists ONLY type 0x21 from 0x31 to a candidate ESC or final
broadcast, priority 20, baud 0, info 0. It rejects ALL 0xCD frames, including valid
CRC empty-mask polls, and cannot produce throttle/reverse channel values. Normal
UART write remains a zero-return stub. The optional HAL transaction entry also
compiles to a zero-return stub in these images, independently of UI checks.
UART4 remains outside SerialManager and PA0 outside PWM/GPIO ownership tables.

Under a future reviewed qualified build, native transport acquires the existing
RX-only backend exclusively, detaches PA0 before reconfiguration, uses AF8/open
drain/HDSEL, disables RE during TX and releases via UART TC IRQ. The existing 3 ms
virtual-timer abort covers a stalled transaction. A new locked 1 kHz service hook
handles delayed TEACK/FIFO filling; 14 bytes fit the 16-byte H7 FIFO. No DMA,
timer-pulse serialization or bit-banged UART is introduced. RX guard errors and
turnaround failures cause fault/RX recovery. The passive consumer is suspended
without ending a leased UART and cannot resume until ownership is released.

Electrical collision detection is NOT guaranteed: RE is off during TX. Software
checks idle level, UART busy/FIFO state, unexpected traffic and errors, but cannot
measure contention current or prove an external buffer released. Scope evidence
and the isolated bus topology remain mandatory. Neither GPIO tests nor open-drain
MCU register readback prove the external board's output type.

## 4. Diagnostic parameters

| Parameter | Default | Meaning |
| --- | ---: | --- |
| SPKT_ENABLE | 0 | Existing receive-only bench mode; 1 required for a handshake request |
| SPKT_PROBE | 0 | Existing GPIO probe; MUST be 0 for the handshake reader |
| SPKT_TLM | 0 | Existing validated/fresh passive telemetry frontend publication |
| SPKT_SHADOW | 0 | Existing read-only Plane observer, no transport |
| SPKT_EXP | 0 | Operator experiment label, never TX authorization |
| SPKT_HS | 0 | NEW index 6: write 1 to request one attempt; consumed to 0 in RAM; stored/startup requests ignored |
| SPKT_HSID | 64 | NEW index 7: decimal candidate 64..79; not an observed device ID |

No original index or vehicle output assignment changed. HS requests require
ENABLE=1, PROBE=0, disarmed state, latched bench arming inhibition, existing SRV
emergency stop and a real unique local system identifier. Requests are rate-limited
and never retried automatically. The build-time AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED
remains 0; setting HS=1 in this image reports BLOCKED without changing RX or driving
the pin. Startup init clears saved HS requests before servicing commands. Wait for
completed startup before reading the command: parameter service can run during
initialization and briefly expose a raw saved value before it is discarded.

HS=0 is the idle command value, NOT an active-session abort. Setting ENABLE=0
aborts the context; keep PROBE=0. The ordinary bench latch still requires disable
and reboot to clear. No arm or forced-arm path is weakened. Probe pull modes must
not be selected with an ESC attached. Do not set SERIALn options to control UART4.

## 5. Mission Planner and physical validation steps

1. Remove all power before rewiring. Keep all motor leads disconnected and
   individually insulated, no propeller/EDF rotor, all actuator plugs removed.
   Disconnect the ESC signal/BEC from the FC before loading new firmware. Save
   current parameters and a known-working rollback APJ as in PREFLASH_PHASE9.md.
2. If choosing to flash manually, use only the final Phase 12 APJ/hash below with
   normal loader board-ID checking (56). No bootloader changes or forced mismatch.
   Ordinary startup can synchronize unchanged bundled IO firmware. USB powers
   the FC; do not connect 6 V BEC positive to the signal or servo rail.
3. With ESC still disconnected, set ENABLE=1, PROBE=0, TLM=0, SHADOW=0, HS=0,
   HSID=64 and EXP=1201. Enable suitable disarmed file logging, retaining previous
   logging settings. Reboot, wait for complete startup, confirm DISARMED and bench
   arming inhibition. Do not release safety, arm or invoke motor/servo tests.
4. Request HS=1 once. In THIS build expect `SPKT HS: BLOCKED; no TX`, then LINK
   BLOCKED and zero TX. This is a command/gate/log check, not an ESC test. Confirm
   the command reads 0 again. No GPIO transition or handshake is authorized by it.
5. **Stop at electrical gate E.** Supply reviewed scope captures and ratings for
   the actual PA0-to-connector path, highs/lows/overshoot during all relevant power
   states, output high impedance and allowed sink/injection current, idle bias,
   buffer direction/OE and bus contention protection. Existing DC levels support
   static logic behavior only. If a waveform/impedance test requires active drive
   or external stimulus, first review a separate controller-only fixture and power
   plan with ESC disconnected. This gate-off image cannot produce that waveform;
   do not pretend a parameter enables it or just change the gate to break silence.
6. After electrical/output-isolation/controller-role review and explicit approval,
   a separately reviewed gate-qualified firmware and its exact hash are required.
   No such flashable image is supplied here. The compile-only objects are NOT
   firmware and must never be flashed. Future timing evidence must show ~8.68 us
   bit period, 8N1, continuous 14-byte frame, real TC before RX, safe last-stop-bit
   release, and no overlapping driver. Test analog levels with a suitable scope;
   a logic analyzer/DMM cannot establish impedance/contention alone.
7. Only in that later approved experiment, with every motor/actuator disconnected,
   remove power and verify FMU PWM OUT pin8=AUX7 and pin10=GND against Holybro's
   keyed orientation. MAIN/I/O is a different connector. Common signal ground;
   BEC positive disconnected/insulated; USB FC and separately reviewed ESC power.
   Do not connect MAIN3 to the ESC. Use the reviewed power sequence and scope the
   bus. Begin ENABLE=1/PROBE=0 and allow idle/RX baseline before one HS request.
8. In a qualified image only: write HS=1 ONCE, do not automate retries or address
   scans. The expected maximum is one query and, after a valid response, one final
   broadcast. Inspect scope plus SPKM/SPKW/raw diagnostics. Own-source 0x31 echoes
   do not count. A peer response must be 0x21, CRC-valid, source matching the chosen
   candidate, destination 0x31, with priority/capability/UID bytes recorded. Do not
   claim success from a transmitted frame, high input level or LINK state alone.
9. Abort on unexpected device/master, low/stuck bus, growing UART/config errors,
   invalid levels, overlap or reset. ENABLE=0 ends the context; all motor leads
   remain disconnected. Follow the reviewed shutdown/power plan, remove power
   before rewiring, save logs/traces and clear HS/ENABLE/PROBE/TLM/SHADOW. Reboot
   before leaving the bench latch. No automatic PWM fallback is introduced.

The physical output arbiter from MAIN3 to AUX7 is still NOT implemented. Existing
emergency-stop suppression is not a persistent IOMCU inhibit acknowledgement;
raw PWM overrides/local IO failsafe paths need separate review. Physical output
isolation is mandatory for bench work. There is no AUX7 motor-command producer
in this phase, and no IOMCU safety changes, conventional servo remapping or Copter
motor adapter. SERVO1/aileron, SERVO2/elevator, SERVO3/function70, virtual SRXL2
throttle 1/reverse 7 and physical AUX7 remain distinct domains.

## 6. Logs, real RX versus echoes, and telemetry

Existing SPKR preserves historical CRC-valid frame type/sensor/handshake source,
separately from accepted parser results. Existing SPKT/SPKH continue. Their long
column labels were shortened to fit DataFlash limits, without changing value order:
SPKH now uses Flg, FE, NE, PE, ORE, OVF, RelErr, Tmo; SPKT uses St, ID, RXB, HWE,
Part, Rej, LRX. Source meanings are unchanged. Mission Planner text fields remain.

NEW SPKM (1 Hz after a request): TimeUS, St, ID, RXB, RXF, CRC, TXF, TXB, Ec, Tmo,
Bad, RxT, TxT. RXB is bytes consumed from the native port; RXF CRC-valid frames;
CRC failed CRCs; TXF/TXB count native TC-completed frames/bytes, not queued writes.
Ec counts own UID/source echoes; Bad unexpected packets. ID is zero until a valid
matching peer response, not the configured candidate. Type fields refer to actual
received/TC-completed packets. UNIT TEST fixtures are never fed into firmware.

State codes: 0 DISABLED, 1 WAITING, 2 QUERY_TX, 3 RESPONSE, 4 CONFIRM_TX,
5 DISCOVERED, 6 TIMEOUT, 7 FAULT, 8 BLOCKED. DISCOVERED records the last completed
attempt, not continuous liveness or an authenticated product/model identity.

NEW SPKW: TimeUS, Gate, Flags, TCms, Fault, FE, Pri, Mask, Baud, UID. Flags bit0
compile gate, bit1 exclusive lease, bit2 native TC complete. Fault is native
transaction failure count, FE cumulative framing indications. Pri/Mask/UID are
from the actual accepted reply; Baud is the selected 115200 after final TC.
SPKH.RelErr also records release/readback errors. GCS summaries are bounded;
the 200 ms intermediate states may be visible only in trace/counters, not live text.

No telemetry polling is enabled. Public SRXL2 Control Data uses ReplyID for
telemetry requests; omitted channels hold prior values. An empty channel mask is
therefore NOT proof of a safe, neutral ESC command or of this Avian's polling
behavior. The native whitelist rejects every 0xCD packet, even valid empty-mask
ones. A separate stage P must review actual target firmware, retained command
state, timing and motor-safety semantics before any control-data experiment.
No throttle/reverse-bearing frame is permitted simply to obtain telemetry.

The existing read-only decoder supports documented ESC sensor 0x20: electrical
RPM field x10, voltage/current x0.01, ESC/BEC temperature x0.1, plus supported
BEC/percentage fields with validity sentinels. Unknown/missing/stale/contextless
values remain invalid. The existing AP_ESC_Telem frontend can publish valid
voltage/current/temperature through its normal paths; mechanical RPM requires a
verified pole count. A handshake alone does not establish the polling context or
produce a measurement. No new GCS VOLTAGE/CURRENT/RPM/TEMP values are emitted from
missing data, and no consumed capacity is fabricated. Goal 2 remains open.

## 7. Changed files and actual validation

New AP_SpektrumSmartESC_Reader.{h,cpp} and tests/test_spektrum_reader.cpp;
AP_SpektrumSmartESC.h/.cpp add immediate CRC-frame access using the existing
buffer/parser, not a second packet framer. Diagnostics.h/.cpp add HS/HSID,
coordinator and corrected log schemas. Receive.h/.cpp add safe suspend/resume.
AP_HAL/UARTDriver.h adds an optional denied-by-default diagnostic transaction and
status fields. ChibiOS DiagnosticRX and SpektrumUART4.h/.cpp add the gated bridge,
TC timestamp, strict handshake whitelist, delayed-ack service and failure counters.
Host register models/tests and bench/receive tests cover the new paths. No receiver
upstream source, board output mapping, IOMCU image or vehicle motor code modified.

Outer build/validation, compile-only, SITL, artifact and inspection scripts are
preserved with logs in logs/spktsmart-phase12-linux. The official-library reference
files and all changed project sources are included in artifact manifest hashes.

Final tests: protocol 32, mock bus 31, control/shadow 19, new reader 9, receive 9;
diagnostics-enabled bench 14 and receive 9: 123 passing GTest executions across
both feature configurations. ASan/UBSan: existing protocol/mock/control 81,
probe policy 10, actual RX register bodies 4, reader 8, native turnaround register
bodies 9: 112 passing cases. 18 hardware resource/configuration checks passed.
The nine reader GTests include real upstream-derived packet layout/CRC comparison;
sanitizer fixtures and register models remain explicitly non-hardware tests.

Both native gate-zero images contain the new logs and command handler. Disassembly
checks show both normal RX write and the optional native handshake entry return
zero/false without peripheral access. The gate-zero native port acquire also
returns false. Standalone gate-1 DiagnosticRX/SpektrumUART4 objects compile against
real STM32 headers, with a compile assertion confirming the intended gate; no
TX-enabled firmware is linked. This does NOT validate electrical operation.

New disarmed SITL integration passes: a saved startup HS=1 is ignored at init;
a subsequent manual MAVLink HS=1 is consumed and BLOCKED. The final run records
7 BLOCKED reader records and 6 independent shadow records with zero TX/RX frames.
It validates command handling/logging, not physical ESC communication.

The live integration test found an overlong new log format that caused a panic;
this was corrected by splitting records. A new schema audit also caught two
pre-existing long SPKH/SPKT labels, now shortened. The final live test and schema
checks pass. Earlier failures are retained, including the C++11 constexpr link
definition correction, compiler-command parser correction and IRQ prototype
warning correction. Final native builds and qualified object checks have zero
compiler warnings. Earlier interim Phase 12 artifacts were NO-GO and are replaced
by the final hashes below; previous phase artifacts remain unchanged.

## 8. Final artifacts and remaining gate

Build environment: existing pinned ardupilot-dev-chibios:v0.1.3 Linux image,
GCC Arm 10.2.1. Four builds passed; later diagnostic-only corrections rebuilt the
two affected custom images. Stock diagnostics are disabled and their excluded
paths were not unnecessarily rebuilt for label changes. No hardware flashed.

| Target | Vehicle | Flash bytes | Free bytes | Static RAM bytes | Compiler warnings |
| --- | --- | ---: | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | plane | 1595916 | 370152 | 146248 | 0 |
| Pixhawk6C-SpktSmart | copter | 1600556 | 365516 | 147336 | 0 |
| Pixhawk6C | plane | 1586680 | 379392 | 141684 | 0 |
| Pixhawk6C | copter | 1590464 | 375604 | 142780 | 0 |

- `custom-firmware/Pixhawk6C-SpktSmart-phase12-artifacts/arduplane.apj`
  SHA-256 `86413df44fb6170befe7d868db75e714bec499da52c252770e060c7bbe1f0345`

- `custom-firmware/Pixhawk6C-SpktSmart-phase12-artifacts/arducopter.apj`
  SHA-256 `76405fcdb8179e94535d0d40f8d99a6de9f390a08c33393e903f15bd9d9f0cf0`

APJ board ID 56 and decoded BIN equality are verified. Saved ELF equals the linked
image and reproduces the payload with normal 0xff gap fill. The manifest and
SHA256SUMS identify this exact final firmware; Gate remains zero.

STOP at physical gate E. The next useful evidence is qualified TX/release/
contention and timing capture, followed by an explicitly approved isolated one-shot
attempt and the actual response bytes. Target-specific polling semantics, output-isolation proof and valid sensor packets
remain open. If a full official master stack is required, obtain its non-public SDK;
the bounded public-spec handshake experiment can be validated separately. Neither physical success milestone
is claimed from simulation or from the user-reported static DC observations.
