# ArduPlane Hobbywing V5.1 — Viper 90mm / Pixhawk6C

## Firmware identity and module rules

- The **ArduPilot vehicle version is authoritative**: `ArduPlane X.Y.Z`, as defined by `ArduPlane/version.h`. Do not fake a version or change DEV to OFFICIAL merely for cosmetic branding.
- Variant name: `ArduPlane-X.Y.Z[_dev]_Pixhawk6C_Hobbywing-V5.1+KST-760us-560Hz`. A module suffix is descriptive, **not** the core autopilot version.
- **ESC product**: Hobbywing Platinum **150A V5.1**, Platinum **V5 series** serial telemetry.
- **KST 760µs/560Hz**: the intended shared feature name is `Hobbywing V5.1 + KST 760µs/560Hz`; no `disabled` suffix. Like other optional ArduPilot features, KST will be controlled through parameters and default to ordinary servo operation. **The KST code has not yet been ported into this branch**, so naming the planned feature does not imply it is executable today. The Viper currently uses original 1520µs servos and standard PWM. Never apply 760µs pulses to these servos.
- **ArduPlane 4.8.0-dev is what this branch actually uses**. Official stable Plane **4.7.3** was announced on **19 Nov 2026**. A build from this branch must keep the `-dev` marker until the code has actually been rebased/ported against verified stable 4.7.3 source, compiled and regression-tested. See https://github.com/ArduPilot/ardupilot/releases .
- No `HWV5`, `HWPM` or `HWRAW` boot/status debug spam in default operation. Standard `AP_ESC_Telem` and MAVLink `ESC_TELEMETRY_*` carry ESC status. Diagnostics available with `SERVO_BLH_HWPMON=1` or `SERVO_BLH_HWRAW=1`.

## Actual current Viper wiring and settings

Hobbywing throttle remains **SERVO3** at the operator-confirmed **333 Hz**. Reverse remains **SERVO11** (scaled RC11; RC-loss returns reverse to a verified in-range PWM input). The custom driver does not control throttle or reverse, it receives serial telemetry. `SERIAL1` is the operator-used logical UART receiving from the ESC; ensure physical controller labels match the specific Pixhawk6C serial mapping.

| Parameter | Viper value | Note |
|---|---|---|
| `SERIAL1_PROTOCOL` | 16 | ESC telemetry receive |
| `SERIAL1_BAUD` | 115 | 115200 baud |
| `SERVO_BLH_HWV5` | 1 | Decode Hobbywing Platinum V5 |
| `SERVO_BLH_HWOUT` | 3 | Telemetry relates to physical SERVO3 |
| `SERVO_BLH_HWRPM` | 1 | Publish motor shaft RPM |
| `SERVO_BLH_POLES` | 4 | Actual 4 motor magnetic poles |
| `SERVO_BLH_HWGAIN` | 1.0 | Do not pretend ESC current calibrated |
| `SERVO_BLH_HWCOFF` | 0.0 | Zero added current |
| `SERVO_BLH_HWPMIN` | 5.0 | Operator's low-current display gate based on *battery PM*, not verified ESC sensor threshold |
| `SERVO_BLH_HWPMON` | 0 | No paired-current debug text |
| `SERVO_BLH_HWRAW` | 0 | No raw frames in normal flight |
| `SERVO3_FUNCTION` | 70 | Normal throttle |
| `SERVO11_FUNCTION` | 150 | Reverse mapped from RC11Scaled |

Keep the operator-verified battery low/critical failsafe, arming, home, RTL, servo directions, RC loss and 6V servo rail settings from the saved physical aircraft parameter file. Landing gear receives its parallel power feed per the owner's established wiring; this UART module changes none of those circuits.

**RPM**: raw electrical RPM ×2 ÷ motor pole count. Example `65710 eRPM × 2 / 4 = 32855 shaft RPM`. Verify motor poles and independent optical tach if changing models. For future heli: motor RPM divided by gearbox ratio = rotor RPM. Maintain the separate headspeed sensor when governing.

**Important**: Observed Hobbywing ESC amps do not match primary power module amps at medium/high loads. Battery failsafe must continue using the primary **power module**, not corrected or uncorrected Hobbywing current. BEC data 0xFF means unavailable, and should not be presented as a measured voltage/current.

## Build and package

```bash
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch integration/viper90-hobbywing-v5-kst
git pull --ff-only origin integration/viper90-hobbywing-v5-kst
source .venv/bin/activate
bash Tools/hobbywing_v5/build_viper_release.sh
```

The package filename and `BUILD_INFO.txt` use the version that firmware actually reports. Backup `.param` and the previous known working firmware before flash. Build does not flash or enable ESC output. Record actual firmware SHA on boot and inspect standard Mission Planner ESC telemetry plus normal ArduPlane flight modes.

## Planned true stable port / shared driver

1. Fetch **upstream `ArduPilot/ardupilot`** and checkout its **Plane-4.7.3** release tag (actual resolved SHA), not this development branch. Do not copy `ArduPlane/version.h` from the stable tree onto a newer dev checkout; that is not a port.
2. Integrate the isolated `libraries/AP_HobbywingV5` protocol parser and host tests. Port the minimal AP_BLHeli hook, source registrations and params to the stable tree, resolving changed APIs/indices. Build Pixhawk6C Plane. Confirm no inadvertent changes to standard BLHeli/ArduPlane behavior with Hobbywing disabled.
3. Carry over the *saved aircraft .param settings* for PWM/RC reverse and failsafes. Verify all channels and timers on real Pixhawk6C. KST will be an optional parameter-controlled function once ported; until then it is not compiled into this branch. Stock Viper servos stay on standard 1520µs PWM.
4. Only then identify the build as `ArduPlane 4.7.3 | Hobbywing V5.1 + KST 760µs/560Hz`, along with unique Git SHA. If firmware itself still says `4.8.0-dev`, do **not** label it stable 4.7.3.
5. For the helicopter, use independently verified **Copter stable** source, then port Hobbywing and KST as *separate optional features*, with KST 760µs/560Hz as a parameter-controlled feature whose default is standard servo operation. Verify Pixhawk4 MAIN/AUX timer grouping, IOMCU, pulserange failsafe, RPM/tach and ESC PWM before enabling the KST mode. Plane firmware cannot be flashed as Copter.

## Normal messages (not developer test data)

- Mission Planner should show ordinary `ArduPlane X.Y.Z` startup and normal ArduPilot status/health/errors.
- Hobbywing measured fields go through ArduPilot's `AP_ESC_Telem` backend: ESC RPM, voltage, current and temperature. Do not add unconditional `HWV5 first`, `HWRAW`, `HWPM` text messages to production.
- Keep diagnostic modes available explicitly for engineering; when both `HWRAW=0` and `HWPMON=0`, this build does not periodically issue the custom diagnostic text.
- Native Mission Planner display of ESC telemetry can differ by version; check ESC telemetry messages and Status fields rather than relying on old custom STATUSTEXT strings.
