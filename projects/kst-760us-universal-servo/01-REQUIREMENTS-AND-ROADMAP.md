# Requirements, scope and execution roadmap

Date: 2026-11-14. Project: **KST 760us Universal Servo Support**. Status: engineering development, not flight-approved.

## Mission

Make ArduPilot correctly control independently configured narrow-pulse analog PWM servos, including servos with **760us neutral**, without changing the default behavior of 1500us servos. **Priority 1 is ArduCopter Traditional Helicopter, first three-servo single-rotor swashplate plus tail.** The implementation must live in common servo layers where possible so future Plane / QuadPlane / Rover integrations are possible. The software is servo-model-neutral: KST is a motivating application, not a hardcoded dependency.

## Non-negotiable invariants

1. A channel's physical pulse width and electrical neutral are independent from a vehicle's internal collective and cyclic mixing units.
2. With default `H_SW_PWM_MODE=0`, legacy helicopter behavior should remain unchanged and pass regression tests.
3. `SERVOx_MIN < SERVOx_TRIM < SERVOx_MAX` on enabled narrow-pulse swash outputs; invalid or absent/duplicate assignments must block arming, not generate arbitrary output.
4. All output functions are mapped by their **assigned motor function**, not assumed physical connector number.
5. At any collective+cyclic extremum, outputs saturate deterministically within physical configured endpoints. Correct clipping/mixing/anti-windup must be evaluated.
6. Reverse is applied once; servo-neutral remains correct when reversed. Mixed conventional/narrow channels operate simultaneously.
7. Collective zero thrust, mechanical midpoint, normalized midpoint, and servo electrical trim are *distinct concepts*. Avoid conflating them.
8. Never alter throttle/ESC, safety switch, arming, motor-interlock or failsafe behavior to make servo testing easier.
9. Firmware version/commit, parameter export, wiring, servo model, BEC, and test evidence must be recorded before any hardware release.

## Project phases & completion gates

| Phase | Work | Gate |
|---|---|---|
| P0 | Source audit + configuration inventory + hardware datasheets | all assumptions tagged verified/open |
| P1 | Compile current code for SITL and Pixhawk4; fix warnings/errors | clean build logs + commit SHA |
| P2 | Pure C++ output mapping tests and servo regression tests | repeatable automated tests |
| P3 | SITL Traditional Heli: H3 swash, manual controls, collective and RSC separation | expected outputs match reference |
| P4 | H4 / dual / advanced, multiple narrow-pulse assignments, edge cases | regression coverage; safe pre-arm checks |
| P5 | Frequency and electrical bench check on Pixhawk4 | measured pulse min/neutral/max + 560Hz period; independent ESC timing |
| P6 | No-rotor mechanical bench; blade pitch and linkage checks | no binding, valid swash/gyro/tail direction, failsafe behavior |
| P7 | Controlled release candidate with rollback image and documented limits | signed-off bench evidence; no unknown critical issue |
| P8 | General Plane / QuadPlane / Rover support | per-vehicle tests and separate releases |

P0 and initial P1 coding have started. **No successful compilation, SITL, or physical PWM measurement is claimed.**

## Definition of done: Traditional Heli

- 760us servo configuration persists after reboot and does not get reset during disarmed output updates.
- 1500us factory/default behavior is bit-for-bit equivalent or within explicitly documented deterministic rounding, and baseline comparison tests pass.
- At normalized `-1/0/+1`, example configured physical outputs are `560/760/960us` (subject to validated mechanical limits), including reversed/asymmetric cases.
- H3 collective at calibrated MIN, MAX and ZERO_THRST corresponds to measured blade angle targets independently of the servo electrical neutral.
- Stable operation across DISARMED, ARMING, STABILIZE, ACRO, ALT_HOLD, POSHOLD, RTL and fail-safe cases appropriate to TradHeli.
- Servo output refresh and actual pulse lengths measured on Pixhawk4; no unsupported timing applied to ESC.
- Validated trim, direction, swash leveling, cyclic bounds, tail compensation, RSC, interlock, gyro and failsafe behavior.
- Tests, screenshots/logs, firmware provenance, and a rollback procedure are archived.

## Decisions/open questions

- **Current implementation uses `H_SW_PWM_MODE`, not a universal `SERVOx_PROTOCOL` option.** This is intentional for phase 1 but is not yet proof of universal support.
- Verify the exact target board ID/target name using `./waf list_boards` and existing known-good build outputs. Do not assume a target identifier.
- Determine actual versions/compatibility of the user’s current TradHeli firmware and `master` branch. Do not flash a master-based binary over an older aircraft without a parameter compatibility/rollback review.
- Confirm all physical servo part numbers and their 760us/560Hz and supply-voltage ratings individually.
- Verify `H_SW_LIN_SVO` treatment of nonlinear geometry and extreme combinations.
- Confirm `H_COL_MIN/MAX` interface metadata and their intended canonical internal 1000–2000 control domain.

## Source references

- `libraries/AP_Motors/AP_MotorsHeli.cpp`, `AP_MotorsHeli_Single.cpp`, `AP_MotorsHeli_Swash.cpp`, `AP_MotorsHeli_RSC.cpp`
- `libraries/SRV_Channel/SRV_Channel.cpp`, `SRV_Channel_aux.cpp`
- https://ardupilot.org/copter/docs/traditional-helicopter-swashplate-setup.html
- https://ardupilot.org/copter/docs/traditional-helicopter-parameter-list.html
