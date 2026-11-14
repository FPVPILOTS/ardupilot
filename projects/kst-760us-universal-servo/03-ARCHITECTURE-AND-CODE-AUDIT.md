# Architecture and design audit

## End-to-end signal chain

```text
TX / RC receiver -> RC calibration + vehicle mode/controls
  -> collective/cyclic/yaw logic and limiters
  -> AP_MotorsHeli_Single::move_actuators
  -> AP_MotorsHeli_Swash::get_servo_out (normalized -1..+1)
  -> AP_MotorsHeli::rc_write_swash
  -> SRV_Channels per-function to per-output mapping
  -> SERVOx reversal, MIN/TRIM/MAX; hal.rcout
  -> timer bank + physical PWM output -> servo -> linkages -> blade angle
```

Servo supply/BEC and safety hardware are separate from the PWM data path.

## Verified root cause and fix path

1. `AP_MotorsHeli::output_disarmed()` calls `init_outputs()`.
2. `AP_MotorsHeli_Single::init_outputs()` resets swash servo output functions via `reset_swash_servo()`.
3. Legacy reset forces 1000–2000us endpoints on swash servos; the tail is configured via angle and does not undergo the same reset.
4. Legacy `rc_write_swash` builds `1500+500*swash_in` and then applies channel trim offset using `set_output_pwm_trimmed`.
5. The development branch introduces `H_SW_PWM_MODE=1`: preserve swash endpoints and feed normalized commands into `SRV_Channels::set_output_norm_trimmed` to map `-1/0/+1` to `MIN/TRIM/MAX`.
6. `H_SW_PWM_MODE=0` leaves legacy mapping and endpoint forcing on.

## Audit of new helper

Expected transfer function with `u=constrain(command,-1,+1)` and reversal `r`:

- `u' = -u` if reversed, otherwise `u'=u`
- `u'<0: PWM=TRIM + u'*(TRIM-MIN)`
- `u'>=0: PWM=TRIM + u'*(MAX-TRIM)`

Example if valid and un-reversed: MIN=560, TRIM=760, MAX=960 -> -1=560, 0=760, +1=960us. With reversal, the extrema exchange. Test rounding and integer overflow carefully.

**Critical review areas:**
- `SRV_Channels::set_output_norm_trimmed` currently writes immediately via `output_ch()`; confirm atomicity/cork-push semantics and whether this matches legacy function.
- Cross-check duplicate output functions, absent functions, H4, H1, dual heli and mixing reversal.
- Multiple logical channels may use different ranges; every matching output must be validated.
- Confirm `H_SW_PWM_MODE` persisted/read in AP_Param and exposed in Mission Planner metadata. Avoid collisions with existing parameter subgroup keys.
- C++ compile check for AP_Param/GCS string-size, const references and all conditional build flags.
- Audit mechanical saturation, normalized linearization and stabilizing controller integrators when endpoint asymmetry is configured.
- Confirm `H_SW_LIN_SVO` uses normalized travel, yet leveling guidance must reference **760us electrical trim** rather than 1500us when mode 1 is selected.
- `H_COL_MIN/MAX` must retain the control-domain scale; changing to 560–960 requires a deeper mixer redesign and is **not** part of phase 1.
- Do **not** change `set_digital_outputs` and ESC/DSHOT scaling to solve analog servo output; these are distinct paths.

## What is not implemented yet

- Hardware 560Hz verification or frequency policy: the SingleHeli code uses `rc_set_freq(mask,_speed_hz)` for multiple heli motor outputs; trace where `_speed_hz` originates and timer-group mapping.
- Complete pre-arm coverage for H4, dual/quad heli, duplicate functions and missing outputs.
- Versioned parameter migration, tests, CI, compiled binaries, or release artifacts.
- Plane/Rover dedicated integration/regression testing.
