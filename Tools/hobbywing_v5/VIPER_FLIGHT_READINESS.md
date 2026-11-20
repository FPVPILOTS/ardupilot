# Viper 90 mm / Pixhawk 6C / Hobbywing Platinum 150A V5.1 — release gate

Status: **NOT FLIGHT CLEARED** until each gate is demonstrated with current hardware, logs and parameter export. This document must not bypass ArduPlane pre-arm checks or mask sensor failures.

## Baseline confirmed by operator (AIMETA 20 Nov 2026)
- ArduPlane 4.8.0-dev custom Hobbywing UART receive-only telemetry.
- SERVO3 throttle (SERVO3_FUNCTION=70), SERVO11 reverse (SERVO11_FUNCTION=150, RCIN11Scaled).
- 333 Hz PWM ESC accepted in bench tests; reverse and RC loss scaling of reverse stays in valid ESC input range.
- Power module provides total pack voltage and current (battery failsafe primary). Hobbywing ESC current is **not** suitable as a battery monitor replacement; 5 A total pack current gate only hides low-load ESC data, it does not calibrate it.
- Onboard BEC set to 6 V; live BEC voltage/current not reported (raw 0xFF).
- Reverse and radio failsafe bench success do not establish autonomous RTL or loss-of-FMU behavior.

## Gate A — reproducible software
- [ ] Save the exact .param export, firmware git SHA, boot log, .apj checksum.
- [ ] Run `bash Tools/hobbywing_v5/run_host_tests.sh`.
- [ ] Run `python3 Tools/hobbywing_v5/audit_viper_plane.py "viper test(2).param" --strict` on **latest actual** .param file; address every BLOCK and review all REVIEW statuses.
- [ ] `./waf configure --board Pixhawk6C && ./waf plane`; confirm build and flash boot SHA.
- [ ] Verify all active output functions, actual PWM frequencies/timer grouping and reverse scaling on the physical Pixhawk 6C. Do not assume 333 Hz per-channel independently.

## Gate B — battery failsafe on primary power monitor
- [ ] Battery monitor is analog PM or other actual power module, **not ESC**, and measures correct whole-aircraft voltage/current with trustworthy calibration.
- [ ] Record actual battery label: 6S pack capacity in mAh, safe remaining reserve (mAh), pack internal resistance/sag and typical RTL energy consumption.
- [ ] Set `BATT_CAPACITY` to real pack mAh; verify `BATT_LOW_MAH`, `BATT_CRT_MAH`, `BATT_LOW_VOLT`, `BATT_CRT_VOLT`, `BATT_LOW_TIMER`, `BATT_FS_VOLTSRC`. No thresholds are selected without sufficient aircraft-specific evidence.
- [ ] Set low action to RTL (`BATT_FS_LOW_ACT=1`) only after RTL is configured, GPS/EKF healthy and sufficient return reserve demonstrated.
- [ ] Choose critical action deliberately. ArduPlane `BATT_FS_CRT_ACT=1` is RTL; `2` is automatic Land and is not a safe default without an autoland mission. Decide using realistic end-of-pack energy and landing possibilities.
- [ ] Test low and critical battery actions without running motor: controlled SITL or safely isolated bench; verify alerts, mode, latching and recovery. Restore original battery thresholds afterwards and compare .param export.
- [ ] Keep BATT arming thresholds appropriate for sagging 6S; do not test by flying a depleted battery.

## Gate C — RC/failsafe/reverse regression
- [ ] RC normal: throttle 0-100%, reverse commanded only on intended channel, output pulses stay in valid ESC range.
- [ ] RC loss with flight controller active: reverse returns to verified non-reverse, flight mode follows configured failsafe, throttle response appropriate.
- [ ] RC recovery: reverse and throttle do not jump unexpectedly.
- [ ] IOMCU/FMU/PWM loss: inspect independently; RC-loss scaling alone does not cover these failures.
- [ ] Test with EDF motor mechanically/electrically isolated before any powered test.

## Gate D — airframe and navigation
- [ ] Calibrate all required accelerometers and compass; confirm GPS home and stable EKF, no persistent prearm warnings.
- [ ] Control surfaces: correct orientation, direction and travel in MANUAL/FBWA; verify transmitter trims and link safety.
- [ ] Check servo rail 6 V under simultaneous servo/gear movement with known voltage rating and electrical capacity.
- [ ] Verify physical reverse switch direction and lockout policy so reverse cannot engage inadvertently in flight.
- [ ] Confirm RTL climb altitude, radius, airspeed/TECS, throttle limits, stall margin, terrain and landing plan, and failsafe mission behavior in SITL.
- [ ] GPS lock/home established **at intended launch location**; RTL is not valid with GPS/prearm errors.
- [ ] Distinguish firmware successful build from hardware test and first-flight clearance. Flight readiness requires supervised ground checks and competent pilot.

## Relevant ArduPilot docs
- https://ardupilot.org/plane/docs/apms-failsafe-function.html
- https://ardupilot.org/plane/docs/parameters.html
- https://ardupilot.org/plane/docs/common-prearm-safety-checks.html
- https://ardupilot.org/plane/docs/starting-up-and-calibrating-arduplane.html
