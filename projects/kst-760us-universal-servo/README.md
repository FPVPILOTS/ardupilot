# KST 760us Universal Servo Support

**Status:** project scaffold / investigation; not yet flight-ready.
**Branch:** `feature/kst-760us-universal-servo-support`
**Base:** `master` of `FPVPILOTS/ardupilot`

## Goal

Provide **generic, per-output support** for narrow-pulse servos (e.g. 760 µs neutral) across ArduPilot vehicle types: ArduCopter (including traditional helicopter swash/tail), ArduPlane (including QuadPlane), Rover, and other consumers of SRV_Channels. The name references the KST use case, but the implementation must be **servo-brand-neutral**.

Preserve 1500 µs defaults and compatibility for existing servos. Support configurations such as `SERVOx_MIN=560`, `SERVOx_TRIM=760`, `SERVOx_MAX=960` **only if the specific servo and mechanical installation permit them**. Do not assume that every KST product is a 760 µs servo or accepts 560 Hz.

## Verified starting points (source inspection)

- `libraries/SRV_Channel/SRV_Channel.cpp`: `SERVOx_MIN` metadata range is 500–2200 µs; `SERVOx_TRIM` and `SERVOx_MAX` metadata ranges are 800–2200 µs. These are parameter *metadata*, not proof that the firmware rejects a lower value at runtime.
- `SRV_Channel::pwm_from_angle()` maps normalized control using the configured MIN/TRIM/MAX, whereas `pwm_from_range()` maps using MIN/MAX. Audit callers and any output clamps.
- `libraries/AP_Motors/AP_MotorsHeli_Swash.cpp`: `H_SW_*_LIN_SVO` help text assumes trim near 1500 µs. Verify whether linearization mathematics depends on 1500 µs or only the documentation does.
- User reports SERVO4 stores 560/760/960 µs, whereas helicopter swash outputs SERVO1–SERVO3 do not retain those values. **Root cause not yet demonstrated.** Check helicopter setup code, defaults/reset logic, front-end validation, and parameter persistence.

## Design rules

1. Per-channel range and neutral; no global switch that changes unrelated outputs.
2. Do not change existing default PWM ranges or servo functions.
3. Pulse width in microseconds and repetition rate in hertz must be configured, validated, and tested separately.
4. A 560 Hz refresh period is about 1785.7 µs. Verify timer groups / HAL constraints, output protocol, PWM multiplexing and actuator limits for each supported board. Do not silently change shared timer-group frequencies.
5. Reject or warn about invalid `MIN < TRIM < MAX` combinations where appropriate; account for intentionally asymmetric throws and output-function semantics.
6. Preserve actuator disarm/failsafe behavior, motor output separation, and output protocol constraints. No automatic arming, motor bypass, or changes to unrelated ESC signals.
7. Avoid hardcoding KST-specific logic or servo ranges into generic vehicle mixers.

## Work packages

### P0 — Reproduce and trace
- Capture parameter set/readback across reboot for SERVO1–4 in helicopter and Plane SITL.
- Trace UI metadata validation versus AP_Param storage versus vehicle setup/initialization overwrites.
- Inspect helicopter swashplate, heli tail, motor functions, and SRV_Channel conversion and output restrictions.
- Record the installed KST servo **exact model**, accepted neutral pulse, travel endpoints, refresh rate, and voltage from manufacturer documentation.

### P1 — Generic channel support
- Adjust metadata/validation for low pulse neutral and endpoints as needed.
- Add automated unit coverage of angle/range mapping, reversal, symmetric/asymmetric travel, parameter save/reload, and unchanged 1500 µs behavior.
- Keep the implementation common to all vehicle types.

### P2 — Vehicle integration
- Copter: traditional heli 3/4-servo swash, tail, and generic servo outputs.
- Plane: fixed-wing primary surfaces, flaps, v-tail/elevons, and QuadPlane servo outputs.
- Rover and other consumers: steering and auxiliary servos.
- Confirm vehicle-specific output mixers do not forcibly restore a 1500 µs neutral.

### P3 — Refresh frequency and hardware
- Verify board/timer compatibility of 333/560 Hz separately from pulse-range support.
- Verify real output pulse width and period with a logic analyzer, oscilloscope, or suitable PWM measurement tool before connecting expensive servo hardware.
- If the user has no oscilloscope, use an inexpensive PWM measurement device or a second microcontroller as a capture tool; do not infer actual timing from UI values alone.

### P4 — Validation and release
- Build Copter/Plane/Rover with the same shared source changes; run relevant SITL, unit, and regression tests.
- Test disarmed, failsafe, reboot, min/trim/max, and mixed 760/1500 µs outputs on different channels.
- Only after bench verification: flight testing and candidate firmware binaries for explicitly supported flight controllers.

## Acceptance criteria

- No loss of servo settings after restart in supported configurations.
- Correct *measured* PWM pulse widths, midpoints and limits per configured output.
- 1500 µs channels remain unchanged when 760 µs servos coexist.
- Heli swashplate and tail retain correct mixing and direction.
- Standard vehicle firmware builds and test suites pass; hardware refresh compatibility is documented, not assumed.

## Initial target

Pixhawk 4 / ArduCopter traditional helicopter with 760 µs swash and tail servos. Expand generic support into Plane and other vehicles without branching the implementation per vehicle.

**Note:** This document establishes project scope. The actual implementation, tests and hardware validation remain to be completed.
