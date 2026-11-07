# Phase 9: live Plane observation, shadow only

2026-11-02, Europe/Amsterdam. AI-assisted changes on the existing development
branch, based on dbe792162d06cab66c3475fd5556bf7a120f119e; no new commit created.
The supplied request ends midway through Task 2. This implements the independently
specified read-only Plane observation work; no missing reverse requirements are
assumed. This is partial Phase 9, not operational digital motor integration.

## Implemented boundary

SPKT_SHADOW defaults to 0. Setting it to exactly 1 enables a read-only 1 Hz Plane
observer, independently of SPKT_ENABLE and SPKT_TLM. It has no transport, no
ownership lease, no packet generation, no writes to SRV_Channels, no PWM mute,
no IO protocol change and no output-enable API. It does not change arming.
The existing RX bench mode and its latched arming veto remain unchanged.

The observer is called in Plane::servos_output AFTER srv.push(). Earlier code
runs set_throttle, voltage/power/min/max limits, suppression, landing behavior,
disarm/ICE behavior, throttle slew, twin/other mixers, manual output overrides,
SRV_Channels::calc_pwm and output_ch_all. Reading get_output_scaled alone would
miss explicit PWM overrides and zero-PWM/disarm paths. Instead this observer
reads the final cached PWM for zero-based output index 2, only if its function is
k_throttle (70). It never uses SERVO1, RC1 or RC3 as a source.

The PWM is the FMU's cached commanded output, NOT measured physical MAIN3 PWM or
an IOMCU acknowledgement. Hardware safety, independent IO failsafe, output masks
and later GPIO/script activity may differ. AFS early termination can bypass the
normal output path and consequently produce no new SPKS record. No downstream
consumer may treat the last logged record as fresh motor authority.

AP_SpektrumSmartESC_Shadow converts configured MIN/MAX range to 0..100%, or a
trim-centred bipolar range to -100..100%, undoing SERVO3_REVERSED. It handles
asymmetric trim spans. The bipolar interpretation follows Plane's
have_reverse_thrust/set_control_channels setup; arbitrary script changes to servo
output scaling are not supported. Zero PWM, bad limits/trim and out-of-range PWM
produce invalid snapshots, clearing the inferred demand. Integer PWM conversion
is quantized; the result is not an unrounded copy of controller demand.

The observer uses the existing AP_SpektrumSmartESC_Control::Demand representation.
The fixed model mapping remains virtual SRXL2 channel 1/index 0 for magnitude,
and channel 7/index 6 for direction. Reverse intent comes from signed final
throttle; permission is observed from Plane::allow_reverse_thrust. The observer
also records armed-and-safety-off, RC health, vehicle failsafe and emergency-stop
states. It never reads RC7 directly. No new RC reverse option is introduced.

IMPORTANT: the live observer does not fabricate calibrated raw SRXL2 values.
Actual Avian zero/full endpoints, forward/reverse raw levels and stop/dwell
behavior remain unverified. No model qualification or ownership flags are forged
to make packet generation succeed. esc_ok and motor_stopped remain false in each
fresh observed Demand. Simulated packet generation and its channel 1/7 safety
suite remain available independently, with synthetic calibration clearly marked.
Live raw channel encoding and reverse actuation remain BLOCKED on validated
calibration, remaining reverse requirements and an actual output coordinator.

## Logs and operator use

SPKS records at most once per second, only when SHADOW=1 and the normal Plane
output path runs. Fields:

- TimeUS: FMU sample time.
- PWM: final cached SERVO3 output, or 0 if not function 70.
- Pct: inferred signed percentage; inspect Flags bit 0 before interpreting it.
- Rev: requested negative direction, not an approved/sent reverse command.
- Flags: bit 0 valid mapping/conversion; bit 1 armed and safety off; bit 2 RC OK;
  bit 3 any vehicle failsafe; bit 4 reverse permitted; bit 5 SRV emergency-stop.

For software-only experiments use the supplied SITL script. It sets SHADOW=1,
SERVO3_FUNCTION=70 and disarmed logging in an isolated simulated parameter store.
No real controller was connected or flashed. On real firmware SHADOW does NOT
suppress existing conventional MAIN3 PWM; it is an observer, not a motor-safe
bench mode or an arbiter. Do not use its name or flag bits as motor authorization.
No physical test is authorized by this document.

## Copter and output ownership

Copter uses Mode::output_to_motors -> AP_Motors::output and vehicle/frame-specific
mixing, spool state and motor function mapping. SERVO3/function70 is not a generic
multicopter motor demand. No Copter shadow caller or motor adapter is installed;
Copter is explicitly unsupported for this feature. The shared parameter is
annotated for Plane only and no Copter motor path is changed. QuadPlane observation
is limited to its explicit SERVO3 fixed-wing throttle, not its VTOL motors.

No physical MAIN3/AUX7 arbiter exists. Creating one cannot be replaced by setting
the simulation Ownership booleans. It requires durable IOMCU inhibit/acknowledgement
across reset/local mixing/failsafe/GPIO, exclusive AUX7 ownership and source-age
validation. That work is still required before any future digital operating mode.
Current Smart TX remains compile-gated off, no physical motor producer exists,
and there is no automatic fallback. Conventional outputs retain existing behavior.

## Modified files

- ArduPlane/servos.cpp: feature-guarded post-output read-only observer call.
- AP_SpektrumSmartESC_Diagnostics.h/.cpp: default-off SHADOW parameter and SPKS log.
- AP_SpektrumSmartESC_Shadow.h: transport-free final-PWM demand conversion.
- tests/test_spektrum_control.cpp: three converter cases, retaining all 16 cases.
- tests/test_spektrum_bench.cpp: actual SRV calculation/override/E-stop regression.
- Outer tools/validate-spktsmart-phase9*.sh and inspect_spktsmart_shadow_log.py:
  reproducible build, GTest, sanitizer and disarmed SITL checks.
- Outer tools/collect_spktsmart_artifacts.py: Phase 9 artifacts and source hashes,
  including the changed Plane source. Earlier artifacts remain intact.

## Build and unit-test evidence

Established Linux Docker image ardupilot/ardupilot-dev-chibios:v0.1.3,
GCC Arm 10.2.1; the same pinned image/build volume as earlier phases.

| Target | Result | Flash bytes | Free flash bytes | Static RAM bytes | Compiler warnings |
| --- | --- | ---: | ---: | ---: | ---: |
| Custom Plane | PASS | 1592460 | 373608 | 145872 | 0 |
| Custom Copter | PASS | 1595744 | 370324 | 146964 | 0 |

Actual APJ/BIN/ELF outputs: custom-firmware/Pixhawk6C-SpktSmart-phase9-artifacts.
APJ board ID 56, decoded length and BIN equivalence checked; SHA256 manifest
records current sources and artifacts. No IO firmware image changed or built.
Standard Pixhawk6C was not rebuilt: diagnostics default off, the added Plane call
is guarded by the existing feature flag, and no standard hwdef/HAL path changed.

Tests actually executed:

- 19 control/isolation/shadow GTests PASS.
- The same 19 standalone cases under ASan/UBSan PASS, -Wall -Wextra -Werror.
- 8 bench GTests PASS, including existing real arming entry-point checks and the
  new actual SRV_Channels test: 75% -> 1750 PWM; explicit 1250 PWM override -> 25%;
  E-stop -> 1000 PWM/0%; zero PWM -> invalid. No physical output is driven.
- 8 receive GTests PASS, retaining startup/shutdown/parser/telemetry coverage.
- Native ELF inspections PASS: zero-return RX write, bench arming veto, physical
  TX/handshake/motor engine absent, gate=0 and native acquire returns false without
  peripheral writes. These checks are not electrical verification.

Logs: logs/spktsmart-phase9-linux; outcomes.csv, native build logs, GTest logs,
control-sanitizers.log, inspection.log and artifacts-manifest.json.

## Live disarmed SITL evidence

A diagnostics-enabled SITL Plane build passed. A 15-second unarmed simulated
Plane run recorded 11 live SPKS DataFlash snapshots, approximately 1 Hz. All
showed SERVO3 PWM 1100, signed demand 0%, no reverse request and Flags=5 (valid
mapping, RC OK, not armed). This proves execution of the real Plane hook and
logger in this disarmed case; it does not prove every flight-mode/failsafe path
or physical compatibility. No armed simulation or flight was performed.

The first attempt waited for a TCP ground-station connection and produced no
samples; logs retain that failure. The retry used nonblocking loopback UDP.
Log inspection initially lacked the installed pymavlink package; it then passed
using the existing repository module via PYTHONPATH=/build/src/modules/mavlink,
without installing dependencies. The reproduction script includes this fix.
See sitl-shadow-console.log, sitl-shadow-inspection.log and
sitl-shadow/logs/00000001.BIN in the Phase 9 log directory. Timeout status 124
is the intentional stop of the disarmed simulator, not a firmware crash.

## Remaining gates

This is a live read-only final-output observation step, not a completed motor
adapter or arbiter. Still required: complete reverse task specification, verified
Avian calibration/settings, complete demand/safety/override review, actual output
ownership, calibrated virtual encoding tests, broader flight-mode/RC/failsafe/reset
SITL coverage and prior AUX7 electrical qualification. Stop and direction-switch
measurements cannot be replaced by fresh-looking ESC telemetry or inferred PWM.
Physical handshake TX, motor throttle and reverse remain disabled. Nothing flashed.
