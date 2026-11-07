# Phase 14B — native SRXL2 reuse and gated ESC output binding

2026-11-03. AI-assisted changes, requiring human firmware/electrical review.
Base commit `dbe792162d06cab66c3475fd5556bf7a120f119e`, branch
`feature/pixhawk6c-spktsmart`, local uncommitted changes. Exact source/artifact
hashes are in the Phase 14b manifest, not a claim of a new committed release.

## Status and limits

**IMPLEMENTED:** production CRC reuse, isolated native UART4 binding of the
existing coordinator, a separate final-Plane writer, explicit Plane reverse
selection, arming-readiness checks, a real optional IO readback client/Owner,
dedicated MAIN3-inhibited IO application, native-session telemetry frontend
publication, and a separately built fixed-pattern waveform laboratory image.

**TESTED:** native structures/globals, command isolation and safety policies,
independent wire vectors, acquisition-time turnaround, disarmed neutral/arming,
start-to-start cadence, IO proof/reset predicates, telemetry validity, laboratory
boot/command behavior, actual driver register models, and disarmed SITL.

**BUILD VERIFIED:** custom and stock Plane/Copter, lab Plane and dedicated F103
IO application; all final compiler warning counts are zero. No device flashed.

**HARDWARE UNVERIFIED:** no external waveform measurement, physical SRXL2
handshake, real ESC measurement or motor control was performed by this phase.
Reported GPIO/DC/RX-only results remain useful input evidence, not TX qualification.

**PRODUCTION ACTIVATION INCOMPLETE:** no validated ESC endpoints, CH7 polarity,
dwell/watchdog semantics or master-role capture exists. There is no shipped
motor-enable parameter or approved activation profile. The new IO image is not
bundled into the ordinary FMU image. A reviewed paired-image deployment and
trusted bench ground/stop-evidence producer are still needed. This is not a
finished, user-selectable LEGACY_PWM/SMART_DIGITAL product or flight-ready firmware.

## Native ArduPilot audit

| Existing source/component | Actual role and reuse |
| --- | --- |
| `AP_RCProtocol/AP_RCProtocol_SRXL2.cpp/.h` | RC-input backend, with a single callback-routing singleton. `_bootstrap()` initializes the global SDK. `capture_scaled_input()` remaps incoming Spektrum throttle/aileron/elevator into ArduPilot RC domains. It is not an ESC-output adapter. Remains unchanged. |
| `AP_RCProtocol/spm_srxl.cpp/.h` | Global `srxlThisDev`, `srxlBus`, channel/telemetry data and callbacks. Reinitializing for an ESC would overwrite the receiver session. Public slave parsing/handshakes are not a full receiver master. Unmodified; native packed types used as independent test references. |
| `AP_RCProtocol/spm_srxl_config.h` | One bus, FC IDs 0x31 and alternate 0x30, no RF, 115200 support. `SRXL_CRC_CALCULATE` calls `crc16_ccitt`. No private master implementation enabled. |
| `AP_Math/crc.cpp` | Production SpktSmart CRC now calls the same stateless `crc16_ccitt(data,length,0)`. The former bitwise routine survives only as an independent host-test oracle. Actual native CRC is also exercised under ASan/UBSan. |
| `AP_RCTelemetry/AP_Spektrum_Telem.cpp/.h` | This checkout's actual Spektrum telemetry location. Sends aircraft telemetry to a receiver/master; not an inbound ESC decoder. Its global sender is not reused as another receiver/ESC session. |
| `AP_SerialManager` | Protocol 23 selects RC input. It does not select an ESC master or reserve a motor output. No receiver or SERIALn parameters changed. |
| `AP_HAL_ChibiOS/UARTDriver.cpp` | Existing half-duplex code disables DMA, waits for both output-empty and transmission-end, and restores HDSEL. Its serial-driver startup and pin ownership are inappropriate for unverified RX-only AUX7 startup. Existing dedicated backend retained; TC/release/error principles reused. |
| `RCOutput_bdshot.cpp` / `RCOutput_serial.cpp` | Relevant ownership/turnaround patterns already audited in Phase 5. Timer waveform encoding is not SRXL2 framing. No DShot pulse encoder reused for UART packets. |

The native RC backend can initiate an advertisement to a listen-only receiver
after its 250-ms listen phase (`process_handshake()`), but that does not make it
the ESC master. Its fallback ID 0x30 is still FC-class, not a receiver ID. The
public SDK master path is conditional on `SRXL_INCLUDE_MASTER_CODE`, which is
not enabled in this integration and references a private implementation.

Sources: [official header](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.h),
[official public source](https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.c),
[Rev K specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf).
Rev K describes receiver/remotes as masters and does not fully specify FC-only
master election. ID 0x31 is an identity, not automatic authority. The existing
bounded FC-master candidate is retained pending vendor/reference/hardware evidence;
no undocumented Avian initialization or fabricated capability discovery was added.

### SERIAL options 4 and 12

`AP_HAL/UARTDriver.h` defines bit 2 as HDPLEX, bit 3 as SWAP:

- **4 = half duplex only**, normally using the UART TX pin for single-wire TX/RX.
- **12 = half duplex + TX/RX swap**, moving the internal signal assignment to the other peripheral pin. It is not an inversion setting or an ESC enable.

AUX7 is PA0/UART4_TX AF8; AUX8 is PA1 and remains PWM8. SWAP would be the wrong
configuration for the selected PA0 connection and could conflict with AUX8.
AUX7's dedicated transport is not a SERIALn port: neither SERIAL option value is
applied to it. The backend explicitly configures HDSEL, PA0 AF8/open-drain, RE
and bias, and rejects unexpected SWAP. Do not change a working receiver to
SERIAL protocol 23/options 4 or 12 in an attempt to configure this ESC output.

## Implemented source changes

Relative to the ArduPilot checkout:

| Files | Change |
| --- | --- |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC.cpp` | Native stateless CRC reuse; independent oracle compiled only in host tests |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_Bus.h` | Optional actual-TC timestamp interface; correct board configuration import before gate defaults |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_Motor.h/.cpp` | Separate motor qualification gate; CH1/CH7 native whitelist; disarmed neutral keepalive, zero at arming edge, arming readiness, timestamped replies and exact start-to-start cadence; explicit idle/dwell direction states |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_ChibiOS.h/.cpp` | Existing Motor + native SpektrumUART4 + concrete IOOwner. IO proof precedes peripheral acquisition. 1-ms service worker with mutex-protected final demand; no receiver singleton reuse |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_IOOwner.h/.cpp` | Real asynchronous companion-image nonce/readback consumer. No default/mock ACK and no automatic PWM re-enable |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_Diagnostics.h/.cpp` | Separate qualified writer/evidence APIs, exclusive parser ownership and arming readiness; SHADOW remains read-only; lab-only one-shot command |
| `ArduPlane/servos.cpp` | Qualified writer hook after final output push, using function 70/index 2 and Plane's configured reverse option/status |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_Receive.h/.cpp`, `..._Telem.h/.cpp` | Native-session fresh complete snapshots share the existing frontend/index guard; stale/partial/conflicting data cannot be refreshed |
| `AP_IOMCU/AP_IOMCU.h/.cpp`, `SmartESCInhibit.h` | Optional asynchronous capability client, semaphore-protected readback, freshness/revocation and no reset retry |
| `AP_IOMCU/iofirmware/iofirmware.h/.cpp`, `ioprotocol.h` | Optional page 209 for immutable capability plus boot-cleared nonce; existing read/write framing/CRC retained |
| `AP_HAL_ChibiOS/hwdef/iomcu-f103-spktsmart/hwdef.dat` | PB8 input, no PWM3/GPIO103 export, original numbering on other outputs |
| `AP_HAL_ChibiOS/DiagnosticRX.h/.cpp`, `SpektrumUART4.h/.cpp`, `AP_HAL/UARTDriver.h` | Fixed-pattern lab API, independent from handshake API; native timestamp/release and strict mutually exclusive whitelists |
| `AP_SpektrumSmartESC/AP_SpektrumSmartESC_Lab.h`, `hwdef/Pixhawk6C-SpktSmart-Lab/hwdef.dat` | Separate waveform-only target; no motor/handshake qualification granted |
| `hwdef/Pixhawk6C-SpktSmart/hwdef.dat` | Explicit motor gate 0; optional IO capability client enabled but dormant until requested |

Tests added/extended: `test_spektrum_native.cpp`, `test_spektrum_lab.cpp`,
`test_spektrum_motor.cpp`, `test_spektrum_receive.cpp`, and the existing
DiagnosticRX/SpektrumUART4 register fixtures. Workspace tooling includes
`build-spktsmart-phase14b.sh`, `validate-spktsmart-phase14b.sh`,
`complete-spktsmart-phase14b.sh`, `finalize-spktsmart-phase14b.sh`,
`inspect_spktsmart_phase14b.py`, `collect_spktsmart_phase14b.py`, and extensions
to the existing collector/auditors. No submodule or receiver implementation changed.

The older Bus/Handshake/Control/Commands simulation APIs remain for regression
tests; they are not additional live master instances. The live candidate is the
existing Motor coordinator plus Reader discovery, not a new parallel parser/SDK.

## Qualified control pipeline

Final Plane `srv.push()` -> SERVO3/function70 cached PWM -> Plane snapshot
conversion -> explicit reverse permission -> fresh ground/stop evidence -> real
IOOwner receipt -> existing Motor -> native UART4 TX/TC/RX -> validated telemetry.

`prepare_motor(profile,qualification,target,uid,nonce)` is an explicit C++
integration entry point, not a normal parameter or automatic boot action. The
ordinary/lab builds reject it before acquiring outputs. `plane_control()` is
separate from `plane_shadow()` and is reached only when both qualification gates
permit it. Changing SHADOW never creates a motor session.
An explicit API selection also latches arming protection when preparation fails;
clearing diagnostic parameters does not silently return to legacy arming.
`SPKI.Phys` describes motor-control availability, not a laboratory waveform.

The final PWM is the existing logical output after Plane processing, not proof
of electrical PWM at a connector; HAL/IO safety and E-stop are separately enforced.
Only index 2/function 70 is accepted. SERVO1/RC1 are not read as throttle sources.
Unipolar/bipolar MIN/MAX/TRIM and reversal are preserved. Invalid mapping/PWM,
failsafe, RC/control loss and diagnostic-mode conflicts revoke authorization.

Plane's `reversed_throttle` and a configured `REVERSE_THROTTLE` aux option (64)
provide the explicit reverse selection. RC7 alone does nothing. For example,
RC7_OPTION=64 is a *later reviewed vehicle configuration*, not set by this phase
and not needed for the waveform test. Plane `allow_reverse_thrust()` remains
mandatory, and bench ground/stop evidence must be supplied explicitly and remain
fresh. It is not inferred from GPS, an estimated landing state or a negative
throttle value. No trusted runtime producer of that bench evidence is shipped yet.

### CH1/CH7 wire mapping

Physical commands require a verified two-channel profile, including a known
forward CH7 level even if reverse requests are disabled. The mask is `0x41`;
values are packed densely in ascending channel order (CH1, CH7), little endian:

`A6 CD 12 00 ReplyID RSSI LossLo LossHi 41 00 00 00 ThLo ThHi DirLo DirHi CRC_H CRC_L`

`ReplyID` is the selected ESC-class address, not a SERVO number. Values range
0..65532 with the bottom two bits zero. Center 32768 is a servo center, not an
assumed ESC idle. For verified endpoints Z/F, throttle is
`Z + 4*round(magnitude*((F-Z)/4))`.

| Demand | Synthetic Z=1000/F=61000 fixture |
| --- | ---: |
| 0% | 1000 |
| 25% | 16000 |
| 50% | 31000 |
| 75% | 46000 |
| 100% | 61000 |

These are not Avian defaults. Profiles remain invalid until independently
qualified. The published Rev K channel example is reproduced byte for byte,
including independently calculated CRC D3 5D; a further test compares the sparse
CH1/CH7 packet against native `SrxlControlPacket` and `SRXL_CRC_CALCULATE`.
The incoming native RC conversion is not misused as an ESC endpoint calibration.

### Timing, ownership and direction

The native binding waits for IO proof, acquires the existing UART4 exclusively,
and uses the existing bounded Reader discovery at 115200. No endless retries or
automatic baud changes. A 1-ms RCOUT-priority service thread is used only by a
future qualified active session: Plane normally runs at 50 Hz and cannot service
the 11-ms bus/5-ms reply windows itself. Main-thread demand is copied under a
semaphore; peripheral initialization/restoration stays on the main thread.

Control cadence is 11 ms **start to start**, not 11 ms after TC. Late bus access
never creates catch-up bursts. Actual TC time is obtained from the native port,
with conservative millisecond quantization; a reply acquired before the main
thread handles completion is still checked against the actual response window.
The native IRQ releases TX on TC, not FIFO-empty. The hardware transaction
watchdog remains 3 ms; discovery is bounded; link/reply errors latch fault.
Real scheduler jitter/turnaround and Avian acceptance remain hardware tests.
An 18-byte control packet exceeds the 16-byte FIFO: host register tests exercise
the 16+2 refill, TC accounting and a missing-tail watchdog. That is not a measured
real-time guarantee. RE is disabled during TX, so there is no claim of reliable
bitwise electrical collision detection; idle/error guards do not replace scope
and contention/current-limit validation.

Direction progression: FORWARD -> REVERSE_REQUESTED -> THROTTLE_TO_IDLE ->
NEUTRAL_DWELL -> REVERSE_ACTIVE; return passes through RETURN_TO_FORWARD and
zero/dwell. A completed zero command plus fresh stopped evidence is required
before selecting direction; zero is also held for a configured dwell afterwards.
CH7 polarity/dwell have no production defaults. Rapid changes cannot bypass dwell.

Initial disarmed frames, where qualified, can only be verified zero/forward.
Arming additionally requires a completed neutral reply, fresh IO ownership,
ground/stop evidence and a fresh zero demand. The arming edge itself must observe
zero; a high stick does not resume thrust. Disarm after operation, RC/control
loss, invalid demand, stale ground/IO evidence, UART error, missing response,
unexpected device/reboot or bad release latches fault. No automatic PWM fallback.

Fault handling stops further commands and releases the line; it does **not**
prove this ESC stops. Actual ESC watchdog/failsafe semantics must be measured
before `esc_loss_behavior` or the motor gate can be qualified. Physical Failsafe
Channel Data is deliberately not whitelisted as a guessed shutdown command.

## MAIN3: real capability, deployment boundary

The dedicated IO application includes the standard F103 IO hwdef, then removes
PB8 and declares it INPUT. PWM3 and GPIO103 are absent. TIM4 still contains
PB9/MAIN4 with map `{255,255,255,3}`; other output numbers stay unchanged.
Generated full PWM/GPIO macros are expanded and audited, including `U` suffixes.
Thus normal, direct, failsafe/mixer, serial/DShot channel requests and GPIO103
requests have no PB8 output mapping in that application. This is stronger than
SERVO3_FUNCTION=0 or a queued disable request. Physical reset/boot behavior has
not yet been measured, including the bootloader interval.

Optional IO page **209**, six 16-bit words, reports version 1, mask 0x0004,
32-bit echoed nonce and 32-bit uptime. Only the nonce words can be written;
mask/version are immutable. A reset clears the nonce to zero. The FMU writes a
new disarmed lease nonce once, polls at 20 ms, requires a fresh (<50 ms) exact
readback and revokes established proof on error/reset/backwards uptime. It does
not silently rewrite a nonce after reset. Releasing the lease also cancels local
capability polling without an output-enable write. No request means no extra IO traffic.
The Owner never manufactures an ACK or re-enables MAIN3 on release. Its `io_boot`
field carries the boot-scoped echoed lease, not an invented hardware-unique boot ID.

**LEGACY_PWM:** shipped normal FMU + stock bundled IO; MAIN3 remains conventional,
Smart motor TX disabled. Existing receiver/servo/IOMCU safety semantics retained.

**SMART_DIGITAL candidate:** requires the dedicated immutable-pin IO application
and real capability proof, then an approved qualified FMU/profile. This is not
a hot-switchable parameter mode: restoring physical MAIN3 requires reviewed stock
IO restoration. The normal ROMFS still contains stock IO and may overwrite a
manually changed IO image at boot. Therefore do not install this experimental IO
image with the ordinary FMU and assume persistent inhibition. A reviewed paired
image/update/rollback policy is a remaining software deployment requirement.
The separate IO artifact was not added to ROMFS and will not be auto-installed.

## Telemetry frontend

The native worker consumes actual timestamped RX only; it does not feed its own
TX into the receiver parser. CRC/type/destination/reply-window checks precede
sensor decoding. Only a new fresh physical-session sample can use the existing
frontend publication guard. Partial/stale/nonfinite snapshots are invalidated;
another producer's index is not overwritten. Index 14 (ESC 15) remains the reserved
frontend slot. Existing passive RX behavior is preserved.

Sensor 0x20 voltage/current/ESC temperature reuse documented conversion. Electrical
RPM is not mechanically scaled using BLHeli assumptions. `SERVO_BLH_POLES`
belongs to BLHeli's conversion/configuration; no parameter was copied into Avian
control. The existing Measurements helper requires independently verified poles
and actual incoming units before converting eRPM to shaft RPM. It also requires
current/baseline qualification before capacity integration. No missing RPM,
capacity, status/error code or temperature is manufactured.

## Builds and automated evidence

Linux Docker/WSL2 image `ardupilot/ardupilot-dev-chibios:v0.1.3`, GCC Arm 10.2.1,
supported waf workflow, no controller devices passed to containers. Logs live
at workspace `logs/spktsmart-phase14b-linux/`; `outcomes.csv`, results JSON,
native/lab/IO logs and image inspection are retained.

Final GTest configurations: native reuse 8, motor 20, isolation/control 19,
mock transport 31, protocol 32, reader 9, normal RX 10, diagnostics bench 14,
diagnostics RX 10, ordinary/lab request/failed-selection tests 2+2: **157 passed**.
ASan/UBSan: model/oracle CRC 20, actual native CRC/model 20, control 19,
mock transport 31, native boundaries 4, native UART register model 9,
waveform-only register model 4, DiagnosticRX register model 4, qualified-control
host register model 2: **113 passed**. Qualified test gates exist only in an x86
fake-register executable; no motor-TX-qualified ARM firmware was produced.
There are 18 existing resource assertions plus the new expanded IO/lab image
audits. Disarmed live SITL produced 7 BLOCKED reader, 7 shadow and 7 SPKI records;
startup HS ignored, manual HS consumed/blocked, Phys=0, no arming/TX.

Initial stock macro/default, lab macro-import ordering, test HAL/GCS/vehicle
fixture and optional timestamp-fallback errors were corrected, with failure logs
retained. The IO resource auditor was strengthened to expand full macro bodies,
rather than matching just a macro's first line. Final cadence was independently
retested in GTest and both CRC sanitizer variants; unchanged suites were not
needlessly rerun after that localized change.

| Build | Flash used | Static RAM | Warnings |
| --- | ---: | ---: | ---: |
| Custom Plane | 1,597,032 B | 146,356 B | 0 |
| Custom Copter | 1,601,152 B | 147,452 B | 0 |
| Stock Pixhawk6C Plane | 1,586,704 B | 141,684 B | 0 |
| Stock Pixhawk6C Copter | 1,590,488 B | 142,780 B | 0 |
| Waveform-only lab Plane | 1,598,152 B | 146,364 B | 0 |
| Dedicated F103 IO application | 40,784 B | 5,188 B | 0 |

Static RAM excludes future qualified worker stack/heap (worker requests 2048 B)
and measured runtime high-water usage. No such motor worker is created in shipped
normal/lab images. Copter remains without a live vehicle motor adapter.

Artifact directories under workspace `custom-firmware/`:

- `Pixhawk6C-SpktSmart-phase14b-artifacts/`: normal protected Plane/Copter APJ/BIN/ELF.
- `Pixhawk6C-phase14b-artifacts/`: stock regression images.
- `Pixhawk6C-SpktSmart-phase14b-lab/`: separate waveform-only Plane, never an ESC-control image.
- `Pixhawk6C-SpktSmart-phase14b-io/`: low-polarity IO BIN/ELF, not FMU firmware; not approved for installation.

Each directory includes a manifest and SHA256SUMS. Normal/lab FMU APJ board ID
is 56. The collector checks decoded APJ payloads against BIN; the source/image
audit compares saved ELFs and Linux snapshot. Exact final checksums are recorded
in those files; no interim artifact should be substituted.

Final SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| Protected Plane APJ | `16f0e32f94b9f9e0b1ea6b9c0a743f5a6020054ec309b87207abd3d1423c1934` |
| Protected Copter APJ | `fa9df03b846a3ef7a084b0d34dccf5fe4139ad64aef73806b896b53f76db5947` |
| Waveform-only lab Plane APJ | `97bde9bbac9f7cd3268abcd335dfd8b34569027cd0e0c5a3b9cd1eb7a8dc75cb` |
| IO development lowpolh BIN | `04920520ae4483e072ffd65b9ce2f632fa814c33e8d7e8159bb282fa8e512c29` |

## Exact next bench procedure: Stage A only

1. Back up parameters and retain a known-good stock rollback image. Disconnect ESC signal **and BEC+**, all servos/actuators and motor power. Remove propeller/EDF rotor; keep motor leads disconnected and insulated. Use USB power only. Do not flash the separate IO image.
2. Review connector orientation: FMU PWM OUT pin 8 is AUX7 signal, pin 10 GND, as established in prior phases. Confirm against the actual housing orientation before attaching scope ground. Do not apply external test signals or unreviewed pull-up voltages.
3. Manually select the **lab directory's** `arduplane.apj`, verify its SHA256SUMS and board ID, and flash only after this wiring review. The agent has not flashed it. A normal ArduPilot bundled *stock* IO update may occur; no experimental IO image is bundled.
4. On USB startup require `SPKL: LAB ONLY; disconnect ESC and actuators`. Arming is inhibited regardless of parameter settings. Check `SPKT_WAVE=0`; stored nonzero requests are ignored. No automatic waveform or handshake occurs at boot.
5. Set `SPKT_ENABLE=1`, `SPKT_WAVE=0`, `SPKT_PROBE=0`, `SPKT_HS=0`, `SPKT_TLM=0`, `SPKT_SHADOW=0`, `SPKT_EXP=14`. Save and reboot with everything still disconnected so the RX-only UART is initialized (ENABLE is reboot-required). Confirm UART status U1 and WAVE=0, with no boot waveform on the scope. Do not change SERVO1/2/3/4 functions, SERIAL protocols/options or RC reverse options. Optional LOG_DISARMED=1 records existing UART diagnostics; retain Mission Planner's tlog.
6. Attach a high-impedance oscilloscope probe to signal/GND and capture startup idle first. A multimeter can check DC/continuity, not UART waveform, high impedance, release, overshoot or contention. A logic analyzer alone does not establish electrical voltage tolerance.
7. After manual approval of this **disconnected-line** experiment, write `SPKT_WAVE=1` once. It is consumed to 0. The only allowed payload is `55 AA 55 AA`, 115200 8N1, no SRXL2 header. Expected bit period is about 8.68 us; four UART characters occupy about 347 us, apart from start/service latency.
8. Require start/result SPKL messages. `TC observed` means MCU TC, not external electrical success. Capture high/low extrema, rise/fall, overshoot, final stop bit and external release. Hardware TX watchdog is 3 ms; diagnostic ownership ends on TC/error or at a 200-ms maximum software window, then restores RX. There is no retry loop; manual requests are separated by at least 5 seconds.
9. Verify no unexpected boot/reboot TX and AUX8 independence. High-impedance proof needs a separately reviewed bounded-load test and board-buffer analysis; do not infer it just from high idle. Do not add external test sources or loads before tolerances/current limits are reviewed.
10. Set ENABLE/WAVE/PROBE/HS=0, power down, and manually return to the protected normal image before reconnecting ordinary aircraft hardware. Reboot clears the diagnostic latch; restore reviewed parameters if needed. Do not leave the lab image in an aircraft.

**Stage A gate:** software/lab artifact ready for the supervised disconnected
measurement, subject to wiring/instrument review. No actual waveform pass is
claimed. Exact board revision, external TX/release traces, levels, loading and
receive-path behavior remain required.

**Stage B NO-GO:** ESC handshake until Stage A evidence is accepted and a separate
manual test is authorized. A future handshake-only build must keep the motor gate
0 and LAB_ONLY=0; one bounded attempt, motor disconnected, no Control Data.
Require actual response/CRC/source/destination/UID and distinguish local echo.
Neither shipped image in this phase can send that handshake.

**Stage C NO-GO:** motor-bearing control verification until real master behavior,
channel endpoints, CH7 polarity and ESC firmware are independently verified. Use
the programmer to record Brake Type=Reverse and Thrust Reverse Channel=CH7,
model/version and actual settings. Do not assume generic Avian firmware variants
are identical. Polling Control Data carries throttle; it is not a harmless
telemetry-only experiment. BEC=6 V is not signal voltage and must not reach PA0.

**Stage D NO-GO:** motor bench test requires separate approval, qualified image
pair, actual watchdog/failsafe/restart tests, trusted ground/stop evidence,
measured scheduler/IO-reset behavior and a means to remove motor power. No
propeller/rotor/fan. No flight or automatic reverse use is authorized here.

Native AUX7 without added flight electronics remains the goal. Actual hardware
milestones—CRC-valid ESC response, real telemetry, forward and reverse motor
response—remain unachieved by this software work.
