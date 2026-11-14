# Traditional Helicopter parameter and state matrix

**Purpose:** Track all setup dependencies instead of patching only SERVOx_TRIM. Names are verified against the current branch source where indicated; other listed vehicle parameters require firmware-version confirmation.

## Units and midpoint: critical distinction

- Physical servo endpoint pulses: `SERVOx_MIN`, `SERVOx_TRIM`, `SERVOx_MAX`, in **microseconds**. Example, if verified for the servo and installation: `560/760/960`.
- Logical helicopter collective range: `H_COL_MIN` and `H_COL_MAX` remain a **1000–2000 internal swash mixing domain** used by `AP_MotorsHeli_Single::move_actuators()`; do **not** set them to 560/960 merely because the servo uses those pulses.
- Logical neutral/half travel: `collective=0.5` is not the same thing as zero lift, nor necessarily the physical `SERVOx_TRIM`, particularly after endpoint/collective geometry adjustments.
- `H_COL_ZERO_THRST` is blade-pitch angle (degrees) at zero thrust, not a PWM neutral setting. `H_SV_MAN=3` commands that blade-pitch reference.
- Legacy `H_COL_MID` was removed (source explicitly reserves index 5). Do not recreate it or assume a parameter by that name exists.

## Setup / test matrix

| Parameter(s) | Meaning and expected validation |
|---|---|
| `FRAME_CLASS`, `FRAME_TYPE` | Select traditional heli configuration; verify vehicle firmware/version support |
| `SERVO1..3_FUNCTION` | Physical swash output assignments as Motor1–3, not physical channel assumptions; check unique assignment |
| `SERVO4_FUNCTION` | Typically Motor4 tail for single rotor; validate assignment and tail variant |
| `SERVOx_MIN/TRIM/MAX` | Electrical pulse and mechanical clearance, independently per servo |
| `SERVOx_REVERSED` | Verify positive cyclic/collective commands and trim remain correct |
| `H_SW_PWM_MODE` (new) | 0=legacy 1000–2000 endpoints; 1=use per-channel MIN/TRIM/MAX for swash; not yet validated |
| `H_SW_TYPE` | H1/H3/H3-120/H3-140/H4-90/H4-45 geometry and motor function count |
| `H_SW_COL_DIR` | Collective travel direction |
| `H_SW_LIN_SVO` | Swash servo linearization; physical trim and geometry must be tested |
| `H_SW_H3_* / H_SW_H3_PHANG` | Generic H3 geometry, phase compensation; do not manually enable H3 subsystem |
| `H_COL_MIN/MAX` | Internal swash collective endpoints; set by measured blade pitch using H_SV_MAN, not by 760us center |
| `H_COL_ANG_MIN/MAX` | Measured minimum/maximum blade pitch in degrees, used for calibrated zero-thrust handling |
| `H_COL_ZERO_THRST` | Blade angle at zero thrust, independent from electrical trim |
| `H_COL_LAND_MIN` | Grounded collective limit in altitude-controlled modes |
| `H_COL_HOVER`, `H_HOVER_LEARN` | Hover collective estimation/learning |
| `H_CYC_MAX` | Cyclic blade-pitch range; full collective+cyclic extremes require clearance tests |
| `H_SV_MAN` | Disarmed swash setup: 0 automated, 1 passthrough, 2 max, 3 zero thrust, 4 min; restore 0 before flight |
| `H_SV_TEST` | Boot-time servo movement; disable for first bench connection |
| `H_TAIL_TYPE`, `H_GYR_GAIN*` | Tail configuration, external gyro behavior and gain output ranges |
| `H_COLYAW` | Collective-to-yaw torque compensation, inspect direction and scaling |
| `H_FLYBAR_MODE` | Flybar vs flybarless control response |
| `H_RSC_*` | Main rotor speed control mode, ramp, runup, throttle curve, governor, interlock interaction |
| `IM_STB_COL_*` | STABILIZE collective-curve mapping: ensure center stick gives intended pitch |
| `RC*_*` and RC mapping | Stick center, calibration, collective control and motor interlock |
| `ATC_*`, `RATE` logs | Attitude/rate response, saturation and integrator behavior in heli |
| Board PWM output configuration | Frequency and shared timer groups, ESC isolation, digital/analog protocols |
| `SERVOx` output monitoring, `RCOU` logs | Compare command PWM to measured electrical pulses, accounting for firmware version |

## H_SV_MAN states (verified in AP_MotorsHeli.cpp)

| Value | Meaning |
|---|---|
| 0 | Automated flight control |
| 1 | Pilot passthrough for mechanical setup |
| 2 | Max collective |
| 3 | Zero thrust *blade pitch* reference |
| 4 | Min collective |

Manual mode may also command tail/other outputs and must be used with propulsion disabled.

## Measurement plan

For each swash servo capture: assigned motor function, connector, servo model, MIN/TRIM/MAX, reversing, servo-arm angle at trim, mechanical min/max, actual blade pitch at H_SV_MAN 2/3/4, and cyclic positive/negative commands at min/center/max collective. Capture tail separately. Save before/after reboot dumps and RCOU logs.

## Known source implementation limitations (as of 2026-11-14)

- Pre-arm check verifies only the first three motor functions; H4 extras and dual rotor need coverage.
- Per-channel helper clamps normalized command but invalid output range currently falls back to a clamped trim; investigate whether pre-arm checks cover every output and whether safe fallback is sufficient during operation.
- New parameter and code are not yet compiled, so generated parameter metadata / GCS visibility is unverified.
- Current `H_COL_MIN/MAX` descriptions say PWM microseconds, although mode 1 uses an internal legacy mixing scale; descriptions may need conditional clarification.
- Source has other hardcoded 1000/1500/2000 references for RSC, ext gyro and digital outputs; separate those from swash functions rather than globally replacing constants.

Official setup reference: https://ardupilot.org/copter/docs/traditional-helicopter-swashplate-setup.html
