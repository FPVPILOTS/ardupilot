# Viper 90 mm — Pixhawk 6C release candidate RC1

## Scope

Hardware-specific ArduPlane build for the **Pixhawk 6C**, Viper 90 mm EDF and **Hobbywing Platinum 150A V5.1**. This is **not** a universal release for every ArduPilot board, ESC, vehicle or airframe. It preserves normal ArduPlane behavior and receives Hobbywing V5 serial ESC telemetry.

Operator-reported completed configuration (AIMETA; not automatically independently retested in RC1):
- Throttle output **SERVO3**, ESC PWM **333 Hz** (timer-group compatibility confirmed by operator on physical board).
- Reverse **SERVO11** via RCIN11Scaled; operator confirmed correct movement and RC-loss reverse signal remaining in ESC range.
- Battery fail-safe low/critical, RTL/home/GPS/EKF, arming and servo configuration reported finished by operator.
- Servo power rail supplied with **6 V BEC** through Pixhawk 6C servo rail; landing gear separately powered in parallel as specified by operator.
- In-flight PID tuning and dynamic flight/EKF optimization deferred to flight-test phase.

## Changes from research branch

- Verbose `HWV5`/ `HWRAW` diagnostic messages are emitted only when `SERVO_BLH_HWPMON=1` or `SERVO_BLH_HWRAW=1`, to reduce telemetry/GCS log noise during normal flight.
- A minimal telemetry link health counter report remains once per minute while diagnostics are disabled.
- **No modification** to SERVO3/SERVO11 PWM, reverse scaling, RC-link failsafe, battery actions, PWM-frequency configuration, servo-rail wiring, or flight controls.
- Optional ESC current suppression `SERVO_BLH_HWPMIN` stays independent of the primary battery monitor. ESC amps still cannot be considered accurately calibrated against battery input amps. Primary battery monitor always governs battery failsafe.
- BEC data with raw value `255` stays invalid/unpublished.

## Build and identify

```bash
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch release/viper90-hobbywing-v5-pixhawk6c-rc1
git pull --ff-only origin release/viper90-hobbywing-v5-pixhawk6c-rc1
source .venv/bin/activate
bash Tools/hobbywing_v5/build_viper_release.sh
# Or attach the current exported .param file to copy/audit it in the artifact:
# bash Tools/hobbywing_v5/build_viper_release.sh "/path/to/current-viper.param"
```

The script runs host tests and waf, then creates `build/viper90-release/rc1-<commit>/` containing the resulting APJ and SHA256 checksums. It does **not** flash, arm or activate motor outputs. Record the boot firmware hash after applying the APJ, and keep a backup of the last known working APJ and parameter set.

## RC1 acceptance evidence

| Item | Status |
|---|---|
| Firmware branch and builder | Created |
| User-reported PWM 333 Hz + reverse + RC-loss behavior | Operator confirmed on previous build |
| User-reported battery failsafe, RTL, GPS/EKF, servo directions and 6V rail | Operator confirmed |
| Latest source host tests | Pending independent execution |
| Latest RC1 Pixhawk6C .apj build and hash | Pending execution |
| RC1 after-flash regressions | Pending on hardware |
| RC1 flight validation | Pending (handled separately) |

Build PASS is not identical to flight validated. No firmware can guarantee operation with arbitrary hardware or parameter combinations. RC1 is a named build for the specified Pixhawk6C target; later generalization should have individual board-specific build/tests.

## Operator RC1 regression (avoid repeating existing configuration/tuning)

After flashing verify version SHA, saved params, one-time bench ESC throttle/reverse and link loss, battery low/critical mode changes (without depleting pack), home/RTL available, and that 333 Hz mode remains enabled. This is **regression of RC1**, not reopening completed settings.

Re-enable research diagnostics only when needed: `SERVO_BLH_HWPMON=1`, `SERVO_BLH_HWRAW=1`. For normal use set both to 0, and keep `SERVO_BLH_HWGAIN=1`, `SERVO_BLH_HWCOFF=0` until the ESC internal current sensor has an independently supported calibration.
