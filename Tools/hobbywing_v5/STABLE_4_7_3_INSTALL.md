# ArduPilot 4.7.3 | Hobbywing V5.1 + KST 760us/560Hz

## Status & provenance

- **Upstream ArduPilot official 4.7.3 common source commit** `d9c5097c3d8333435712ffed86bca8ce00c4bb46`.
- **Plane**: original `ArduPlane/version.h` from official Plane-4.7.3, target `Pixhawk6C`.
- **Copter**: original `ArduCopter/version.h` from official Copter-4.7.3, target `Pixhawk4`.
- The custom branch carries the receive-only Hobbywing V5 decoder and its AP_BLHeli integration. **Do not alter firmware version headers or standard default parameter tables** to disguise upstream provenance.
- **KST support is not yet ported into this new stable branch**. Product family label includes KST, but this exact build does NOT claim operational 760us/560Hz support until implemented and tested. Keep original normal servo behavior.

## Build both (no vehicle-specific defaults embedded)

```bash
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch stable/ardupilot-4.7.3-hobbywing-kst
git pull --ff-only origin stable/ardupilot-4.7.3-hobbywing-kst
source .venv/bin/activate
bash Tools/hobbywing_v5/build_stable_pair.sh
```

The script checks both real `4.7.3 OFFICIAL` firmware version headers and requires official common base SHA to be an ancestor; runs host tests; compiles Plane/Pixhawk6C, Copter/Pixhawk4; creates per-target .apj binaries, SHA256SUMS and BUILD_INFO.txt. **The firmware binaries only exist after this build successfully runs**. Factory default parameters remain as shipped in ArduPilot; no airframe config is baked in.

## Viper 90 mm / Pixhawk6C / Hobbywing Platinum 150A V5.1

These parameters are **operator-specific after installation**, not firmware defaults. Use existing backed-up, validated .param values for servo ranges, RC-failsafe, reverse, RTL and battery failsafe; do not apply a generic all-parameters overwrite.

| Setting | Viper value / explanation |
|---|---|
| `SERIAL1_PROTOCOL` | `16`, ESC telemetry (check actual TELEM UART serial mapping) |
| `SERIAL1_BAUD` | `115` = 115200 baud |
| `SERVO_BLH_HWV5` | `1`: Hobbywing V5 receive-only decoder |
| `SERVO_BLH_HWOUT` | `3`: physical ESC throttle output SERVO3 |
| `SERVO_BLH_HWRPM` | `1`: publish motor shaft RPM |
| `SERVO_BLH_POLES` | `4`: verified magnetic rotor pole count for Viper motor |
| `SERVO_BLH_HWGAIN` | `1.0`: no unverified current correction |
| `SERVO_BLH_HWCOFF` | `0.0`: no current offset |
| `SERVO_BLH_HWPMIN` | `5.0` A primary PM current display gate; this is NOT current calibration |
| `SERVO_BLH_HWPMON` | `0`: paired PM engineering text disabled |
| `SERVO_BLH_HWRAW` | `0`: raw frame debug disabled |
| `SERVO3_FUNCTION` | `70`: ESC throttle |
| `SERVO11_FUNCTION` | `150`: reverse via RCIN11Scaled, mapped to RC11 |
| ESC PWM | operator-confirmed 333 Hz; preserve previously validated output group settings |
| Servo rail | 6V servo supply, independent gear power wiring as built by operator |

Power module remains responsible for primary aircraft pack current/voltage and battery failsafes. The Hobbywing raw ESC amps are NOT validated as accurate against power-module DC measurements. Raw byte `255` indicates unavailable BEC readings; do not publish synthetic 6V BEC telemetry.

**RPM equation:** mechanical shaft RPM = electrical RPM × 2 ÷ magnetic motor poles. For 4 poles and raw 65710, mechanical motor = 32855 RPM. Validate tach if necessary.

### Firmware verification

After compiling and flashing the correctly targeted APJ, ordinary ArduPlane startup reports `ArduPlane V4.7.3` with a custom commit SHA, and ESC data appears through standard ESC telemetry. With `HWPMON=0` and `HWRAW=0`, custom debug STATUSTEXT is not emitted. Any legacy parameters absent on stable 4.7.3 must be reviewed during migration; never assume a .param export is compatible just because its name is identical. Preserve original Viper setup and verify SERVO3/11 and 333Hz behavior after moving firmware base.

## Blade 500 / Pixhawk4 / Hobbywing Platinum 80A

- Install only `ArduCopter-4.7.3-Pixhawk4-...` built from common stable source.
- Firmware uses standard ArduCopter defaults. Airframe heli parameters are applied separately from the saved heli `.param`.
- Motor ESC UART telemetry `SERIALx_PROTOCOL=16`, `SERIALx_BAUD=115` and `SERVO_BLH_HWV5=1` only after verifying the **actual 80A ESC** transmits compatible V5.1 32-byte frames. Set `SERVO_BLH_HWOUT` to the real motor/HeliRSC servo output, not automatically 3.
- `SERVO_BLH_POLES` = actual heli motor pole count, `SERVO_BLH_HWRPM=1` only after verification; motor shaft RPM is not rotor headspeed RPM without a gear ratio. Keep independent headspeed sensor for closed loop governor unless separately validated.
- **KST 760us/560Hz remains unavailable in this exact port**. Do not apply 560/760 parameter values to normal servos; leave stock ranges and rates. After completing the KST patch on this same official 4.7.3 base, add opt-in parameters with defaults preserving normal PWM on Plane and Copter, test IOMCU and FMU groups on Pixhawk4, and rebuild both.
- Standard servo power, battery failsafe, flight control and governor parameters are vehicle-specific, not included in firmware defaults.

## Pending engineering verification

1. Build stable branch and resolve any 4.7.3 API incompatibility revealed by compilation (do not transplant 4.8 source wholesale).
2. Flash and regression-test ESC telemetry, speed, motor reverse, failsafe and 333Hz output on Pixhawk6C; stable-base differences may change timer grouping or PWM setup.
3. Port tested KST patches only with new per-output opt-in, default 1520us PWM; validate on Plane and Copter including IOMCU independently.
4. Mark combined two-module variant complete only once both builds and hardware checks pass.

Official releases: https://github.com/ArduPilot/ardupilot/releases
