# Phase 14C — final mapping and hardware readiness

Implementation: 2026-11-03; final artifact audit: 2026-11-04. Base revision: `dbe792162d06cab66c3475fd5556bf7a120f119e`,
branch `feature/pixhawk6c-spktsmart`, with uncommitted project changes.
This work is AI-assisted and requires human firmware/electrical review.

**IMPLEMENTED / SOFTWARE TESTED:** final-output mapping audit, direction consistency
fix, replay rejection, explicit IO image CRC qualification and fail-closed lease
handling. **HARDWARE UNVERIFIED:** external UART waveform/release, physical IO
inhibition/reset behavior, Avian handshake, telemetry, endpoints, reverse and motor
shutdown. No controller was flashed or connected to a build container.

Normal firmware retains `HARDWARE_TX_VALIDATED=0` and `MOTOR_TX_VALIDATED=0`.
The native motor path exists in source behind those gates; it is not an active
motor backend in the delivered normal firmware. No production activation profile
or trusted ground/motor-stop evidence producer is supplied. Copter motor control
through Smart remains unsupported.

## 1. Authoritative mapping and ordering

| Domain | Selected value | Meaning |
|---|---|---|
| RC input | `RCMAP_THROTTLE=3` | Pilot throttle input to Plane; never read directly by the Smart adapter |
| Vehicle function | `SRV_Channel::k_throttle` = 70 | Final propulsion function, shared by manual and automatic controllers |
| Configured servo | `SERVO3_FUNCTION=70`, index 2 | Cached final PWM used as the calibrated observation point |
| Legacy connector | MAIN3 | Conventional PWM when stock IO is installed |
| SRXL2 throttle | CH1, index 0 | Normalized final thrust magnitude encoded using a verified ESC profile |
| SRXL2 direction | CH7, index 6 | Separate, explicitly authorized direction value |
| Digital connector | AUX7, PA0 AF8/UART4 | Reserved single-wire transport, physically gated |

`ArduPlane/Plane.cpp` runs `update_control_mode`, `stabilize`, then `set_servos`
as fast tasks on each main loop. `mode_manual.cpp::update()` obtains the pilot
throttle through `get_throttle_input(true)` and writes **function 70**.
`Attitude.cpp::calc_throttle()` writes the TECS demand to the same function.
`mode_guided.cpp::update()` can instead write a fresh guided throttle request.

`servos.cpp::set_throttle()` applies the mode's battery compensation and throttle
limits, then suppression/landing/takeoff behavior. `set_servos()` subsequently
handles disarm output policy, slew limits, and configured ICE overrides.
`servos_output()` performs mixing, optional manual PWM overrides, `calc_pwm()`,
`output_ch_all()`, and `srv.push()`. The existing Smart hook is **after that push**.
No hook relocation was needed.

`SRV_Channel::calc_pwm()` applies the channel's range/angle/reversal and cached PWM
override rules. `SRV_Channel_aux.cpp::output_ch()` writes the resulting value.
The Smart snapshot is a requested output, not a measurement of an external pin.
Hardware safety and IO failsafe may suppress physical PWM separately; the native
adapter therefore also receives `is_armed_and_safety_off()`, RC health, vehicle
failsafe and emergency-stop state. A valid numeric conversion alone grants no TX.

`AP_SpektrumSmartESC_Diagnostics.cpp::plane_control()` reads only servo index 2
and verifies function 70 through `AP_SpektrumSmartESC_Plane::convert()`. A remap
of throttle to another SERVO is deliberately rejected in this deployment: the
physical inhibit currently covers MAIN3 only. The protocol model itself accepts
a transport-independent normalized demand. Generalizing servo selection without
generalizing the ownership mask would be unsafe.

Manual PWM overrides are reflected in the final cached output. Twin engine,
QuadPlane/tailsitter, scripting overrides and arbitrary output remaps have no
approved Smart motor profile. Their normal ArduPilot behavior is preserved.

## 2. Conversion, packet data and corrected direction behavior

For unipolar output, percent is `100*(PWM-MIN)/(MAX-MIN)`; reversed output uses
`100-percent`. Bipolar output uses each side of TRIM separately, then applies
SERVO reversal. The final signed percent becomes magnitude `abs(percent)/100`.
Invalid endpoints, out-of-range/zero PWM, wrong function and wrong servo index
are rejected. Disarm/RC/control/ownership freshness are enforced independently.

Reverse requires an explicitly configured Plane `REVERSE_THROTTLE` auxiliary
function (option 64), Plane's `reversed_throttle`, `allow_reverse_thrust()` and
fresh externally qualified ground/stop evidence. Raw RC7 has no direct role.

**Corrected in 14C:** a selected reverse switch previously allowed a *positive*
final automatic throttle demand to become reverse magnitude. The adapter now
rejects this disagreement. Negative final demand requires explicit permitted
reverse; positive demand requires forward; zero can enter an authorized direction
transition. Reverse selection with a unipolar output configuration is rejected.

The existing motor coordinator controls zero thrust, actual-stop evidence, dwell,
direction selection and a further zero dwell. States include FORWARD,
REVERSE_REQUESTED, THROTTLE_TO_IDLE, NEUTRAL_DWELL, REVERSE_ACTIVE,
RETURN_TO_FORWARD and latched FAULT. It does not infer reverse authorization
from a negative number. Reconnection requires a new neutral authorization.

Normal two-channel Control Data is 18 bytes, mask `0x00000041`; CH1 then CH7
are packed densely, little endian. Channel values are multiples of four in
0..65532. CRC-16/CCITT uses the same `AP_Math::crc16_ccitt` routine as native SRXL2.
The published Rev K packet and native packed-structure tests from 14B are retained.
Throttle uses `zero + 4*round(magnitude*(full-zero)/4)`. Forward and reverse use
separately verified CH7 values; neither polarity nor dwell is a production default.

| Logical demand | Actual SITL final PWM (fixture MIN=1000/MAX=2000) | Virtual magnitude | Encoded CH1 in mock profile only |
|---|---:|---:|---:|
| 0% | 1000 | 0 | 1000 |
| 25% | 1250 | 0.25 | 16000 |
| 50% | 1500 | 0.5 | 31000 |
| 75% | 1750 | 0.75 | 46000 |
| 100% | 2000 | 1 | 61000 |

The last column uses the existing synthetic profile, **not Avian calibration**.
It proves conversion/packing, including CRC, while CH7 remains 2000 in that
fixture. Existing reverse tests independently exercise the synthetic 60000 reverse
level and synthetic dwell; those numbers must not be installed as Avian settings.

## 3. IO ownership, firmware identity and reset behavior

The dedicated `iomcu-f103-spktsmart` application removes PB8/MAIN3 from the PWM
and exported GPIO tables and initializes it as input. PB9/MAIN4 remains mapped.
Neither normal PWM writes nor IO failsafe/mixer writes can recreate the absent
MAIN3 hardware mapping. This is an application-level guarantee from generated
configuration; bootloader/reset pin behavior and the external connector still
require oscilloscope verification.

The ordinary Pixhawk6C and custom diagnostic FMU images still bundle stock IO.
Legacy MAIN3 operation is retained there. No parameter makes the dedicated IO
application dynamically switch between PWM and Smart. Returning to legacy MAIN3
requires stock IO and a deliberate reboot/validation procedure.

`AP_SpektrumSmartESC_IOOwner` uses the real optional `AP_IOMCU` client. The native
backend now requires a nonzero, explicitly reviewed `Qualification::io_firmware_crc`.
Before issuing a nonce and at each validation poll, the client reads
`PAGE_SETUP/PAGE_REG_SETUP_CRC` and compares the observed IO application CRC with
that expected value. It does not learn an acceptable identity from the attached
IO. This read-only check never invokes the uploader. CRC detects a different
build; it is not cryptographic authentication against malicious firmware.

Page 209 must also return exact capability VERSION=1, mask=4, the current nonce,
and progressing IO uptime. The CRC is calculated by IO over application flash,
including FF padding to `HAL_IOMCU_APP_SIZE_MAX` (0xF000 bytes for this target).
It differs from the artifact SHA-256 and must not be confused with it.

**Corrected in 14C:** unchanged uptime no longer refreshes an authorization.
Wrong image CRC, missing/bad capability response, failed nonce write, unexpected
version/mask, reset nonce, backward/frozen uptime or lost IO communication revokes
the lease. This includes failures before the first accepted capability response.
An old transaction cannot overwrite the state of a newly requested nonce.
Revoked leases stop polling and require a fresh, explicit disarmed request.
Cancellation retains the last cookie and revokes it, so the immediately released
cookie cannot be reused to authorize an old acknowledgment.
Receipt age is limited to <50 ms; ordinary unrequested operation adds no IO traffic.

IO reset clears the nonce. FMU reboot resets the native owner, selection and
qualification; the shipped gates remain zero. The normal firmware updater skips
its CRC-update path after a watchdog reset, so the Smart path must use its own
identity/capability validation rather than relying on `crc_is_ok`. Fault/release
never sends a MAIN3-enable operation. These software properties do **not** prove
physical IO reset behavior or that an ESC stops when UART traffic stops.

### Image-pair installation and rollback boundary

**No approved digital FMU/IO pair is supplied. Do not install the experimental IO
image with the ordinary diagnostic FMU.** `Pixhawk6C/hwdef.dat` bundles
`Tools/IO_Firmware/iofirmware_f103_lowpolh.bin` as `io_firmware.bin` and a separate
stock DShot image. `AP_IOMCU::check_crc()` can replace a manually installed
experimental IO with that bundled stock image at the next ordinary boot.

The following procedure is checked against source/build conventions; it has not
been executed on this controller and is **not a physically verified installation**:

1. Before any future installation, disconnect ESC signal, all motor power, BEC
   positive and every actuator. Remove propeller/EDF rotor; insulate motor leads.
   Save full parameters, known-good board-56 FMU APJ and its SHA-256, IO selection
   (`BRD_IO_ENABLE`, `BRD_IO_DSHOT`), boot messages and prior measurements.
2. A reviewed future paired FMU must embed the exact inhibited low-polarity IO BIN
   under the selected ROMFS filename, have the matching padded application CRC
   in its approved profile, reject unsupported IO/DShot selections and retain
   motor TX=0 for initial installation. Verify decoded ROMFS bytes and manifest
   hashes before presenting that image for explicit installation approval.
3. Do not use `BRD_IO_ENABLE=100` to suppress the ordinary CRC updater as a pairing
   shortcut. It is a developer bypass, not evidence of correct output ownership.
4. With explicit operator approval, install only the reviewed paired FMU using
   the normal Pixhawk loader. Its deliberate IO update must be expected, logged
   and uninterrupted. No IO binary is a substitute for a Pixhawk FMU APJ.
5. Before allowing any ESC signal connection, verify the real IO application CRC,
   capability/version/mask, fresh nonce/uptime and physical MAIN3 high impedance.
   Test ordinary reset, watchdog reset, IO communication interruption and cold
   power cycle while monitoring MAIN3 and AUX7. Wrong/missing evidence is a hard
   lockout, even if Mission Planner reports firmware startup success.
6. Rollback while still completely disconnected: manually install the recorded
   stock Pixhawk6C board-56 APJ, restore normal `BRD_IO_ENABLE=1` and the recorded
   supported IO/DShot selection, then perform an ordinary cold boot so the stock
   CRC/updater path can restore its matching IO. Confirm IO update/CRC success.
   Compare saved parameters; set bench commands/ENABLE/SHADOW/PROBE/HS/WAVE to zero
   as appropriate and reboot to clear latched bench inhibition.
7. Confirm legacy MAIN3 PWM and safety/disarm/failsafe electrically with all motor
   and actuator loads disconnected. Restore aircraft wiring only after the normal
   configuration and outputs have been independently checked. Failed IO recovery
   remains NO-GO; do not bypass pre-arm checks or blindly apply another IO image.

## 4. Avian-specific evidence

Spektrum's [receiver port mapping guide](https://wiki.spektrumrc.com/spektrum/receiver-port-mapping-guide)
explicitly places Smart ESC throttle on SRXL2 CH1 regardless of physical receiver
port and describes selectable reverse channels 5–9. CH7 is therefore a valid
requested virtual direction channel. The [SPMXAE1130A product page](https://my.spektrumrc.com/Products/Default.aspx?ProdID=SPMXAE1130A)
identifies reverse support and 3S–6S operation. These facts do not establish the
Avian's accepted bus-master role, actual endpoint calibration or response timing.

Record the actual programmer/SmartLink readback: model and ESC firmware revision,
Brake Type=Reverse, Thrust Reverse Channel=CH7, throttle calibration and restart
options. The BEC's 6 V setting does not define signal voltage. The published
SmartLink programming-guide download was not retrievable during this audit; no
numeric reverse polarity, dead band, neutral dwell or communication-loss timeout
was promoted to verified status. Existing project research remains preserved.

Independent reference capture and the actual ESC must establish CH7 inactive/active
levels, transition behavior, required zero time, power-cycle/reconnect behavior and
watchdog shutdown before a profile is authorized. Stopping transmissions alone is
not accepted as a demonstrated stop command. Generic SRXL2 failsafe packets are
not sent as an invented Avian shutdown semantic.

## 5. LAB waveform procedure — existing Phase 14B artifact

Use the retained `custom-firmware/Pixhawk6C-SpktSmart-phase14b-lab/arduplane.apj`:

```text
97bde9bbac9f7cd3268abcd335dfd8b34569027cd0e0c5a3b9cd1eb7a8dc75cb
```

This is board ID 56, waveform-only. It never permits SRXL2 handshake/control TX.
The new 14C LAB build is regression evidence, not a requirement to repeat already
recorded measurements. No measurements have yet been supplied for either image.

1. With everything unpowered, disconnect ESC signal/BEC leads and all actuators.
   Motor leads remain disconnected and individually insulated. Verify connector
   orientation against the Holybro drawing: FMU PWM OUT pin 8 is AUX7, pin 10 GND.
   Use a verified breakout/pin numbering; do not infer numbering from wire color.
2. Use USB power only. Inspect for shorts and record PCB revision if available.
   Attach a high-impedance oscilloscope probe to AUX7 and a safe common ground.
   Verify grounding before attaching an earth-referenced scope. A logic analyzer
   alone cannot prove analog voltage, overshoot or high impedance.
3. Manually flash the identified LAB APJ only after the disconnected wiring review.
   The experimental IO image is not needed. Capture cold boot and reboot on AUX7;
   there must be no test burst. LAB arming inhibition is unconditional.
4. Set `SPKT_ENABLE=1`, `SPKT_PROBE=0`, `SPKT_HS=0`, `SPKT_TLM=0`,
   `SPKT_SHADOW=0`, `SPKT_WAVE=0`, `SPKT_EXP=14`. Save/reboot disconnected because
   ENABLE requires reboot. Confirm UART initialization and WAVE still zero.
5. Trigger the scope, then manually set `SPKT_WAVE=1` once. It is consumed, never
   restored as an authorization at boot. Expect only bytes `55 AA 55 AA`, 115200
   baud, 8N1, about 8.681 us per bit and 347.22 us total wire time (40 bits).
   No A6 SRXL2 header, throttle or reverse packet is permitted.
6. Record high/low levels, 10–90% rise/fall times, overshoot/undershoot, any gaps,
   final stop bit, line release and RX restoration. TC status is an internal
   observation and must be correlated with the external trace. The hardware TX
   watchdog is 3 ms; diagnostic ownership is bounded to 200 ms. No automatic retry.
7. A steady high line after TX does not prove high impedance. Demonstrate release
   only with a reviewed, current-limited impedance measurement after the board's
   signal path and tolerances are established. Do not inject a voltage or fit an
   arbitrary pull-up/load to an unqualified connector. No test signal may exceed
   the verified pin/buffer tolerance.
8. Check AUX8 and MAIN3 with loads disconnected: AUX8's existing PWM ownership
   must remain independent; no unrequested AUX7 startup pulse may appear after
   cold boot, reboot, command timeout or diagnostic exit. An ESC-disconnected
   trace cannot prove absence of contention with the ESC's own driver.
9. Return WAVE/ENABLE/PROBE/HS to zero, power down and restore the normal protected
   firmware before ordinary wiring. Keep ESC signal disconnected pending review.

| Measurement | Required evidence | Status |
|---|---|---|
| PCB revision and buffer/voltage path | Actual revision + reviewed path/tolerance | UNVERIFIED |
| Idle/high/low and overshoot | Calibrated external scope trace | UNVERIFIED |
| Baud/framing and four-byte pattern | Scope or sufficiently fast analyzer decode | UNVERIFIED |
| Open-drain release/high impedance | Reviewed impedance test + scope timing | UNVERIFIED |
| TC-to-RX and reception path | External timing correlated with firmware status | UNVERIFIED |
| Boot/reboot/timeout | Captures showing no unsolicited transmission | UNVERIFIED |
| AUX8 independence and MAIN3 inhibit | External output/reset/failsafe measurements | UNVERIFIED |
| ESC-connected contention | Separately approved current/timing experiment | NOT AUTHORIZED |

Static pull-up/pull-down and RX-high measurements from earlier phases remain useful
input-path evidence. They do not replace these dynamic TX/release measurements.

## 6. Software verification and artifacts

The persistent evidence directory is `logs/spktsmart-phase14c-linux/`.
Final results:

| Check | Actual result |
|---|---|
| Focused GTests | 79 passed: native 8, motor 22, isolation/control 19, bench 14, receive 10, normal context 3, LAB context 3 |
| Independent SRV test selection | The real SRV mapping test also passed alone in both normal/LAB contexts; not added again to the 79 total |
| ASan/UBSan | 36 focused scenarios passed: 22 motor/command, 4 native/IO boundary and 10 actual IO-client method scenarios |
| Before/after ASan/UBSan probes | Two fault predicates exercised in two temporary builds; old result 3, corrected result 0 |
| Existing diagnostic SITL | 7 blocked-reader, 7 shadow and 7 final-intent records; startup/manual HS blocked; disarmed |
| New mapping SITL | Five manual levels, RC1 isolation, guided limit/RC3 independence, disarm; 67 shadow and 67 intent records |
| Resource checks | 18 configuration/AUX7/bidirectional-DShot candidate assertions passed |
| Generated image inspection | Ordinary TX stubs, LAB handshake stub, both motor gates zero, IO PB8 exclusion and PB9 retention passed |
| Saved artifact audit | All 92 manifest source files match the Linux snapshot; saved Plane/Copter ELFs regenerate exact BIN/APJ payloads |
| ROMFS/IO pairing audit | Embedded stock IO bytes verified; experimental IO differs, so ordinary FMU + experimental IO is deliberately NO-GO |
| Python validation | Syntax and focused flake8 checks passed after formatting corrections |

Builds used the existing `ardupilot/ardupilot-dev-chibios:v0.1.3` Linux environment,
Arm GCC 10.2.1 and the supported waf workflow. All six configurations below built
successfully with **zero compiler warnings**. Static RAM is data+BSS; it is not
measured peak heap/stack use or a hardware scheduling/latency result.

| Firmware | Flash bytes | Static RAM bytes |
|---|---:|---:|
| Pixhawk6C-SpktSmart Plane | 1,597,108 | 146,360 |
| Pixhawk6C-SpktSmart Copter | 1,601,228 | 147,448 |
| Standard Pixhawk6C Plane | 1,586,704 | 141,684 |
| Standard Pixhawk6C Copter | 1,590,488 | 142,780 |
| Pixhawk6C-SpktSmart-Lab Plane regression | 1,598,228 | 146,368 |
| Experimental IO low/high polarity applications | 40,784 each | 5,188 each |

Final saved artifacts (paths relative to the workspace):

| Path | SHA-256 |
|---|---|
| `custom-firmware/Pixhawk6C-SpktSmart-phase14c-artifacts/arduplane.apj` | `3e797a78422f4f8a3e63389eea3b03445683efbb2db9ae442029ef0863f75309` |
| `custom-firmware/Pixhawk6C-SpktSmart-phase14c-artifacts/arducopter.apj` | `c43ed21cd74b9f1dae2b9d2102f25eeb944731dc370d95f8c0e4f9230b0380b2` |
| `custom-firmware/Pixhawk6C-SpktSmart-phase14c-lab/arduplane.apj` | `122de8ebaaed2377aab16f5abe4ed98d722b19455d0eb29e92d058b74dca352b` |
| `custom-firmware/Pixhawk6C-SpktSmart-phase14c-io/iofirmware_lowpolh.bin` | `04920520ae4483e072ffd65b9ce2f632fa814c33e8d7e8159bb282fa8e512c29` |

Both FMU targets use board ID 56; hashes and directory labels distinguish ordinary
and LAB images. Each artifact directory includes BIN/ELF, `SHA256SUMS` and the
source/build manifest. The IO binary is not a Pixhawk FMU flash image.

The ROMFS audit decoded the actual generated header, verified its stored CRC and
compared the bytes with stock IO. Stock low-polarity IO SHA-256 is
`38c89aec93f3acd69c9f6065347e2090615e4f1e87bc72eefdd3dca347188f1e`,
application CRC32 `1afa33f5`. The experimental IO application CRC32 is `517abce5`.
Both CRC calculations cover 61,440 application bytes including FF padding and
agree with the native build CRC implementation and an independent zlib calculation.
These values identify files; no physical controller readback has been performed.

One early test compile rejected a floating-point equality assertion; it was
corrected to a tolerance check. A CRLF shell redirect prevented the first final
inspection invocation; the script was normalized and the actual image inspection
was rerun successfully. Initial Python indentation/line-length findings were also
corrected. These are not hidden hardware or firmware successes.

The new before/after experiment compiles temporary header copies under ASan/UBSan.
With the reconstructed 14B predicates, both wrong-direction and frozen-IO cases
are accepted (exit bitmask 3); with current predicates both are rejected (0).
This is a focused predicate comparison, not a historical firmware or hardware run.

The IO-client harness extracts the four actual method bodies from `AP_IOMCU.cpp`
and runs them with a deterministic clock/UART fixture. It checks absent requests,
armed/zero-identity rejection, wrong firmware, exact receipt expiry, frozen uptime,
unexpected initial version, IO reboot, canceled-cookie reuse, failed nonce write,
communication loss and changed image identity. This tests client control flow;
it does not exercise real UART scheduling, semaphore contention, IO firmware or
connector behavior. The physical IO acknowledgment remains unmeasured.

SITL exercised real RC3 through Plane at all five levels, changing RC1 independently.
It also exercised GUIDED 75% under THR_MAX=50 with RC3 both low and high (PWM1500),
then GUIDED25% (PWM1250). 67 SPKS and 67 SPKI records showed final conversions and
Phys=0/Rev=0. The test deliberately used ARMING_REQUIRE=0 in the isolated simulator
to permit legacy simulated output; it restored 1 and verified disarming afterward.
It is not a disarmed physical PWM test or a complete TECS autonomous-flight test.
The separate existing diagnostic SITL test remained disarmed throughout.

## 7. Changed files and remaining gates

Production changes in this phase:

- `AP_SpektrumSmartESC_Plane.h`: direction agrees with the final signed demand.
- `AP_IOMCU/SmartESCInhibit.h`: strict progress and exact nonzero image CRC predicate.
- `AP_IOMCU/AP_IOMCU.h/.cpp`: read-only expected-image check and lease failure latches.
- `AP_SpektrumSmartESC_Motor.h`: explicit reviewed IO CRC qualification field.
- `AP_SpektrumSmartESC_IOOwner.h/.cpp`, `AP_SpektrumSmartESC_ChibiOS.cpp`: carry that
  expected image identity to the real client before ownership is possible.

Tests/tools: `test_spektrum_motor.cpp`, `test_spektrum_native.cpp`,
`test_spektrum_lab.cpp`, workspace `validate-spktsmart-phase14c.sh`,
`finalize-spktsmart-phase14c.sh`, `test_spktsmart_phase14c_mapping.py`,
`test_spktsmart_phase14c_regressions.py`, `inspect_spktsmart_phase14c.py`, and
`test_spktsmart_phase14c_io_client.py`, plus phase-aware artifact collectors/auditor.
Earlier phases and unrelated user changes are retained.

**GO for the documented disconnected-ESC waveform measurement only after the
operator checks the bench wiring. NO-GO for an ESC handshake today.** That next
gate requires external voltage/release/turnaround evidence, a reviewed connected-bus
contention plan and explicit approval for one bounded handshake with motor leads
disconnected. A CRC-valid response must come from the ESC, not local echo.

Motor throttle/reverse remains **NO-GO** until real protocol interoperability,
endpoint/CH7 calibration, shutdown/restart behavior, the paired IO deployment and
physical ownership tests pass. A separately approved motor bench test requires
fresh ground/stopped evidence, no propeller/EDF rotor, supervised power removal
and verified fault/recovery behavior. No automatic reverse or flight approval is
provided. Conventional Plane/Copter behavior and RX/shadow diagnostics remain the
only deployed vehicle modes in the protected builds.
