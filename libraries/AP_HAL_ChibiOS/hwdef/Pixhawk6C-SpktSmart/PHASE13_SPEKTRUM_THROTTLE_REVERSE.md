# Phase 13: prepared throttle/reverse control path

Date: 2026-11-03. AI-assisted implementation; human firmware/safety review required.
Branch: `feature/pixhawk6c-spktsmart`, base commit
`dbe792162d06cab66c3475fd5556bf7a120f119e`, with local changes, not a new committed release.

## Verdict and scope

**IMPLEMENTED / SOFTWARE TESTED:** reusable Channel Data encoder, dedicated-bus
master/control coordinator exercised through mock transport, explicit direction
transitions, output-inhibit receipt contract, fault latching, and final Plane
output-to-intent conversion. The actual Plane shadow hook evaluates that intent
each output cycle and logs it at 1 Hz. Existing phases/tests are retained.

**BUILD VERIFIED:** custom Plane and Copter, zero compiler warnings. Saved
APJ/BIN/ELF artifacts match the Linux build snapshot. No firmware was flashed.

**HARDWARE UNVERIFIED / NO-GO:** physical UART transmission, ESC handshake,
channel interpretation, motor response, reverse and failsafe stopping behavior.
No received physical SRXL2 frame has been supplied. The reported DC/RX-only
measurements establish useful input-path evidence, not output qualification.

**INCOMPLETE:** production vehicle motor backend, real reset-safe IOMCU output
arbiter, qualified native Control Data sender, and electrically active laboratory
firmware. This is not a completed production implementation. The new coordinator
is not instantiated as a live vehicle motor backend. Merely linking an unused
coordinator into an APJ would not make it functional; the audit explicitly checks
that it remains absent from vehicle ELFs until its missing safety dependencies exist.

## Implemented code

Paths below are relative to the ArduPilot checkout.

| File | Change |
| --- | --- |
| `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_ChannelData.h/.cpp` | Rev K normal/failsafe codec, 32-bit masks, ascending-channel serialization, range/reserved-bit validation |
| `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Commands.cpp` | Existing simulation command API reuses that codec; existing packet behavior/tests retained |
| `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Motor.h/.cpp` | Prepared master/control session, abstract port/owner, bounded discovery, TC accounting, telemetry response window, direction/fault policies |
| `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Plane.h` | Transport-independent final SERVO3/function70 conversion; no RC input API; explicit reverse-source permission |
| `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Diagnostics.h/.cpp` | Per-output-cycle read-only Plane intent; `SPKI` log; no transmitter or output writer added |
| `libraries/AP_SpektrumSmartESC/tests/test_spektrum_motor.cpp` | 16 focused GTests and standalone sanitizer fixtures |

The existing `ArduPlane/servos.cpp` hook remains after `calc_pwm()`,
`output_ch_all()` and `srv.push()`. It observes the cached final channel PWM,
including output limits/overrides/suppression already applied by that path. The
new adapter inverses MIN/MAX/TRIM and output reversal; malformed/zero PWM is
invalid, not motor-neutral evidence. Other throttle outputs/QuadPlane/Copter
motor mappings are not generalized into SERVO3.

Workspace tooling added: `tools/validate-spktsmart-phase13.sh` and
`tools/inspect_spktsmart_phase13.py`. Artifact collector and preflash auditor
accept phase 13. The existing SITL check optionally validates `SPKI` with
`SPKT_TEST_PHASE13=1`. The older symbol auditor now matches a symbol's owning
class, rather than erroneously rejecting `Control::Demand` in a read-only
function's argument list. All existing native write-stub checks remain.

Standalone validation scripts for phases 4, 5, 8, 9, 10, 11, 12 and channel
isolation add the codec translation unit needed by the reused Commands encoder.
No existing tests were removed or weakened. No HAL, RCOutput, IOMCU, hwdef,
parameter index or bundled IO firmware was changed during phase 13.

## Wire protocol and channel domains

Authoritative sources:

- [Official SRXL2 specification, Rev K](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf): sections 6, 7.2, 7.6 and 7.7. Archived locally in `sources/Spektrum-SRXL2/specification.pdf` and `.txt` at workspace level.
- [Official public header](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.h): device classes, packed payload structures, baud constants, private master include.
- [Official public implementation](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.c): handshake parsing, telemetry replies and master limitations.
- [Target ESC product](https://www.spektrumrc.com/product/spektrum-accessories-avian-130-amp-brushless-smart-esc-ver-a-3s---6s/SPMXAE1130A.html): model-specific programming differs; generic SRXL2 compatibility is not proof of target settings.

The public header still references an unavailable private master implementation.
This phase supplies an independently implemented, deliberately restricted
candidate coordinator, not that private SDK or a verified interoperable master.
Rev K section 6 describes receiver/remotes as masters and does not establish a
complete FC-only election procedure. `0x31` identifies an FC unit, **not automatic
master authority**. The dedicated-bus FC-master role requires vendor confirmation
or independently observed compatible traffic before physical command use.

| Domain | Assignment |
| --- | --- |
| Plane SERVO1 / SERVO2 / SERVO4 | Aileron/elevator/rudder remain untouched |
| Plane SERVO3 | Index 2, function 70, authoritative final PWM observed |
| RC1 / RC7 | No direct input to the new adapter; RC7 is not implicitly reverse |
| SRXL2 CH1 | Index 0, calibrated throttle magnitude |
| SRXL2 CH7 | Index 6, separately configured and authorized direction |
| Physical transport | Future AUX7/PA0/UART4 only; ordinary firmware remains RX-only |

For CH1+CH7 the mask is `0x00000041` (little endian bytes `41 00 00 00`).
Values follow CH1 then CH7, regardless of SERVO or RC numbering. A normal packet
is 18 bytes:

`A6 CD 12 00 ReplyID RSSI LossLo LossHi 41 00 00 00 ThLo ThHi DirLo DirHi CRC_H CRC_L`

The codec also supports command 1, Failsafe Channel Data, with the same field
layout. During normal data omitted channels hold their previous value; in
failsafe data omitted channels should be disabled. **Neither an empty mask nor
stopping transmission is accepted as proof that this ESC stops.** No physical
failsafe or control packet is sent by this build.

The protocol range is 0..65532, lower two bits zero, center 32768. Rev K's example
uses 10912 and 54612 for approximately -100%/+100% transmitter travel. Those are
not proven Avian throttle-calibration endpoints. For verified endpoints Z/F:

`wire = Z + 4 * round(magnitude * ((F-Z)/4))`, magnitude 0..1.

| Demand | Synthetic test Z=1000, F=61000 |
| --- | --- |
| 0% | 1000 |
| 25% | 16000 |
| 50% | 31000 |
| 75% | 46000 |
| 100% | 61000 |

These fixture numbers are deliberately **not production ESC defaults**. Profile
verification defaults false. CH7 forward/reverse values and neutral dwell also
have no production defaults. Negative final Plane demand is rejected by the new
adapter unless a separate configured reverse request and Plane permission exist.
Even a valid intent never supplies ground, stopped-motor or IO ownership evidence.
The retained older `Control` simulation API is not the production authority.

The new codec exactly reproduces Rev K 7.7.1's published 28-byte example. Its
CRC is `D3 5D`, independently computed with Python `binascii.crc_hqx(bytes, 0)`.
This verifies the documented wire encoding, not Avian interoperability. No
independent ESC/receiver capture was available.

## Master, turnaround and telemetry model

The coordinator reuses the Phase 12 Reader/parser rather than duplicating its
framer. It uses candidate FC ID 0x31 and one explicitly selected ESC-class
address 0x40..0x4F, 115200 baud only. Startup listens 50 ms, queries once, accepts
only the matching handshake response, and broadcasts the selected baud rate.
Discovery is bounded to 200 ms, with no retry/election loop or PWM fallback.

The port retains its packet buffer through physical TC, then restores RX before
accepting a response. Queued bytes are not counted as transmitted. Transmission
has a length-derived watchdog; collisions, failed release and UART errors revoke
the session. Idle guards are conservative 2 ms acquisition guards. The underlying
Phase 12 native port retains its 3 ms watchdog, open-drain/HDSEL preparation and
handshake-only whitelist; it has not been extended to physical motor frames.

After discovery the mock coordinator schedules Control Data with ReplyID=the
selected ESC and channel values, not an empty-mask poll. Cadence is an explicit
11 ms bench policy, never a catch-up burst. A requested telemetry response has
a conservative 5 ms window; 50 ms without peer evidence ends the session. These
policies are not claimed to be validated against this ESC firmware. Malformed
frames do not refresh the link. Unexpected valid frames, reboot handshakes or
local control echoes fault; bytes during owned TX are not ESC evidence.

Sensor 0x20 reuses the existing decoder. Freshness is 50 ms in the coordinator.
Telemetry packets contain a destination, not an authenticated sender ID; a
dedicated verified bus is essential. Mock measurements are never forwarded to
`AP_ESC_Telem`. Existing passive physical RX remains the only frontend publisher.
No actual voltage/current/temperature/RPM was received in this phase. Mechanical
RPM, capacity and fault fields must not be invented from unavailable metadata.

## Direction and failsafe policy

States: DISABLED, DISCOVERY, IDLE, FORWARD, REVERSE_REQUESTED,
NEUTRAL_TRANSITION, REVERSE_ACTIVE, RETURN_TO_FORWARD, FAULT.

1. Explicit disarmed start is required. Non-simulated ports are unconditionally refused.
2. Initial stopped, zero-magnitude, forward intent is required before any nonzero command.
3. Direction changes command zero in the current direction first.
4. Actual stop evidence must be fresh (<50 ms), with a TC-completed zero command and uninterrupted configured dwell before changing CH7.
5. The selection frame still carries zero throttle. A second dwell holds zero after selection; repeated toggles cannot bypass it.
6. Ground authorization must be explicit and fresh (<100 ms), for the entire bench session, not inferred from GPS or a landing flag.
7. Armed state, RC/control health, demand age (<50 ms), finite range and Plane reverse permission are checked before servicing a pending TX.
8. Disarm, RC/control/failsafe loss, stale ground evidence, invalid demand, ownership loss, UART/collision/release fault, missing reply or unexpected protocol latches FAULT.
9. Recovery requires a new explicit disarmed session, discovery and initial neutral. No automatic thrust resumption or legacy PWM fallback occurs.

This proves command *authorization* in software. Fault handling releases RX and
stops further queued commands; it does **not** prove the real motor stops.
`esc_loss_behavior` qualification is mandatory, and no production source supplies
it. The correct target-ESC idle/failsafe packet behavior must be measured before
any native control sender or operating motor is permitted. A failed line release
remains latched even across a new start; do not recover a questionable pin by
blindly reconfiguring it.

## MAIN3 ownership: implemented contract, missing physical backend

The abstract Owner receipt requires:

- Matching session epoch, nonzero IO boot identity, unchanged IO boot and age <50 ms.
- MAIN3 inhibited, including direct PWM and IO failsafe/mixer output paths.
- AUX7 reserved exclusively for the digital transport.
- Release must never automatically re-enable MAIN3.

Tests invalidate each field, reset the simulated IO identity, expire the receipt
and verify command revocation. These receipts are **mocks**, not IO readbacks.

Actual source audit: `AP_IOMCU::disable_ch()` only changes a local mask and
queues `IOEVENT_SET_CHANNEL_MASK` (`libraries/AP_IOMCU/AP_IOMCU.cpp`).
`iofirmware.cpp` applies `reg_setup.channel_mask` and separately maintains
failsafe PWM/mixing. That interface does not return a durable per-channel
inhibit/epoch/boot acknowledgement. Neither `SERVO3_FUNCTION=0`, E-stop, nor
an asynchronous disable request proves the requested reset-safe physical arbiter.

A production backend therefore needs a reviewed IO protocol/firmware change:
boot-safe MAIN3 suppression, explicit legacy-mode restoration while disarmed,
acknowledged inhibit across normal/direct/failsafe/mixer paths, restart detection
and a means to prevent IO reset from emitting MAIN3 pulses before FMU detection.
A fresh receipt alone is insufficient to guarantee that last requirement.
This needs actual IOMCU reset/failsafe tests. No placeholder ACK or automatic
IOMCU firmware update was added. Conventional MAIN3 configuration remains intact;
its physical behavior was not re-measured here.

## Test/build evidence

Logs: workspace `logs/spktsmart-phase13-linux/`.

| Test | Result |
| --- | --- |
| New prepared master/codec/Plane tests | 16 GTests passed |
| Existing channel isolation/control/shadow | 19 passed |
| Existing mock transport/commands | 31 passed |
| Existing protocol | 32 passed |
| Existing reader | 9 passed |
| Normal RX | 9 passed, separate `gtest-receive-normal.log` |
| Diagnostics-enabled bench / RX | 14 + 9 passed |
| Total final GTest configurations | 139 passed |
| ASan/UBSan new motor / existing control / existing mock transport | 16 + 19 + 31 = 66 passed |
| Distinct hardware resource assertions | 18 passed |
| Actual disarmed SITL Plane | 7 BLOCKED reader records, 6 SPKS and 6 SPKI records; no arming/TX; startup HS ignored; manual HS consumed/blocked |
| Native safety inspection | Gate=0, write and handshake entry return-false stubs, no physical UART4 TX or motor coordinator/encoder linked |
| Artifact audit | All 66 manifest source files match Linux snapshot; saved ELFs regenerate exact BIN/APJ payloads |

A GTest float-equality compilation failure was corrected in the test. The first
symbol audit falsely matched a parameter type; its owner-aware correction passed
without changing firmware or weakening native stub assertions. Failure logs are
retained. The build/test driver reached successful tests and builds, then exited
nonzero on that initial audit; the corrected audit was run separately with exit
0. Final native images were not rebuilt merely to fix the audit tool.

| Target | Flash used | Free flash | Static RAM | Compiler warnings |
| --- | ---: | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart Plane | 1,596,476 B | 369,592 B | 146,272 B | 0 |
| Pixhawk6C-SpktSmart Copter | 1,600,564 B | 365,508 B | 147,360 B | 0 |

Built using Docker/WSL2 image `ardupilot/ardupilot-dev-chibios:v0.1.3`, digest
`sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`,
GCC Arm 10.2.1. Docker Desktop was initially stopped and was started; no BIOS,
system firmware or flight controller was modified. No hardware devices were
passed to the build containers. Copter's active Smart motor mapping stays absent.
Stock Pixhawk6C was not unnecessarily rebuilt: no shared HAL, IO or stock hwdef
changed; previous Phase 12 stock build evidence remains separate.

Saved workspace artifacts: `custom-firmware/Pixhawk6C-SpktSmart-phase13-artifacts/`:

- `arduplane.apj`: `1c565a9faa0c5c2affa80287d0b86ad6f4ac489ea85f191dcae2826e530e569d`
- `arducopter.apj`: `af1a920687f50adc3b8db0dfce633b8b24abae70bb55af71f91be1033a847539`

Both board IDs are 56. Manifest includes sizes/source hashes; SHA256SUMS covers
the APJ/BIN/ELF files. These are flashable **protected observation builds**, not
active motor-control firmware. No active laboratory APJ was produced.

## Mission Planner: current build, observation only

1. Disconnect ESC signal, BEC positive and all actuators; retain parameter backup and known-good stock rollback image. Motor leads remain disconnected/insulated and no rotor/propeller is installed. Initially use USB only.
2. Verify APJ SHA-256 and board identity. Manually load the protected Plane APJ if desired; nothing in these tools flashes it. Normal ArduPilot parameter storage and bundled standard IO handling still apply; back up before changing firmware.
3. Keep `SERVO3_FUNCTION=70`, existing surface functions, ranges/reversal and conventional MAIN3 wiring/configuration. Do not configure a second motor signal on AUX7 or change SERIALn assignments.
4. Defaults are `SPKT_ENABLE=0`, `SPKT_TLM=0`, `SPKT_EXP=0`, `SPKT_SHADOW=0`, `SPKT_PROBE=0`, `SPKT_HS=0`, `SPKT_HSID=64`. No indices/defaults changed. There is no `SPKT_MOTOR` or active reverse parameter.
5. For disarmed observation use `SPKT_ENABLE=1`, `SPKT_SHADOW=1`, `SPKT_PROBE=0`, `SPKT_HS=0`, `SPKT_TLM=0`; select a new `SPKT_EXP` session number. Arming inhibition/E-stop remains latched for the bench session. Keep all actuators isolated.
6. With `LOG_DISARMED=1` and DataFlash logging available, inspect SPKS (signed PWM-derived preview) and SPKI (`Valid`, normalized `Mag`, explicit `Rev`, `Phys`). SPKI `Phys` must be 0 and current `Rev` 0. SPKS negative intent is not permission to reverse.
7. SPKT/SPKR show real receive activity only. A manual HS command remains BLOCKED; do not use it to attempt electrical measurements with this protected build. Silence from an unpolled ESC is not proof of a failed input.
8. To end, set ENABLE/SHADOW/PROBE/HS to 0, power down, restore normal wiring only after review and reboot to clear the diagnostic arming latch. To roll back, manually flash the saved stock image and restore the reviewed matching parameter backup. No automatic rollback or output-mode transition is implemented.

## Remaining stage gates and ESC configuration

**A — software:** prepared policy/vector tests and observation build pass;
production ownership/vehicle writer and ESC-specific master semantics remain
incomplete. This stage is not a certificate of interoperability.

**B — electrical, NO-GO:** qualify the exact board revision/output buffer path.
With ESC disconnected, a separately reviewed one-shot waveform-only lab sender
is still required. Do not repurpose the existing validated-hardware macro as a
normal enable option. Review the line's rated input/output tolerance, output
buffer direction, pull-up/loading and contention current limits before driving.
Measure external AUX7 at the verified connector orientation (FMU PWM OUT signal
pin 8, ground pin 10), not just the MCU GPIO register:

- Scope high/low extrema, overshoot, rise/fall time and loaded idle at 115200 8N1.
- Correlate last stop bit, TC and external high-impedance release; distinguish a driven high from released open-drain idle with a reviewed bounded load.
- Verify the external-to-PA0 UART receive path and error counters with qualified test levels.
- Check AUX8 independence and no startup, reboot or unintended repeat transmission.
- Record board revision, wiring, instrument bandwidth/sample rate and full traces; a DC multimeter or logic analyzer alone cannot prove electrical tolerance/contention.

**C — handshake, NO-GO:** only after B passes and a separate manual authorization:
motor leads disconnected/insulated, BEC+ isolated, disarmed, arming inhibited,
one bounded handshake attempt, actual TC/RX capture, CRC/source/destination/UID
checked, local echo excluded. No startup retry. The existing protected APJ cannot
perform this attempt; a qualified handshake-only build must be reviewed separately.

**D — command verification, NO-GO:** obtain receiver/ESC reference captures or
vendor confirmation for the FC-only master role, CH1 endpoints, CH7 polarity,
telemetry scheduling and actual failsafe/watchdog behavior. Control Data contains
throttle values even when used to request telemetry: no physical polling is
authorized as a harmless diagnostic shortcut. Verify these frames with motor
physically inhibited and explicit command-stage approval.

Using the ESC programmer, record actual model/firmware, Brake Type=Reverse and
Thrust Reverse Channel=CH7, plus readback of endpoints and direction values.
Do not change ESC settings automatically. The target product uses model-specific
programming; the requested settings do not establish wire-level polarity or dwell.
BEC=6 V says nothing about signal-pin voltage, and BEC+ must never reach PA0.

**E — motor, NO-GO:** requires explicit later approval, no propeller/EDF rotor/fan,
verified IO arbiter, independent stop evidence, ground-only authorization,
measured direction dwell and a tested means to remove motor power. Verify zero,
quarters/full command response, return to forward, RC/control loss, UART failure,
FMU/IO resets, ESC brownout, reconnect and no restart at nonzero demand. No flight
or automatic reverse authorization is supplied by this phase.

No real ESC handshake, telemetry or motor-control success is claimed. Native AUX7
with no added flight hardware remains the architecture goal; the present evidence
does not establish whether additional electronics are necessary.
