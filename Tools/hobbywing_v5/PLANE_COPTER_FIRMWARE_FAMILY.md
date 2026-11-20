# Firmware family: ArduPilot X.Y.Z | Hobbywing V5.1 + KST 760us/560Hz

Single source revision, separate binaries, **stock ArduPilot factory defaults** for each vehicle. No preloaded .param profiles. Do not relabel development firmware as a stable release: the exact ArduPlane and ArduCopter version is read from each vehicle's version.h. Model-specific instructions are outside firmware.

| Target | Vehicle | Board | Hardware | ESC PWM | Servo mode |
|---|---|---|---|---|---|
| Plane | ArduPlane | Pixhawk6C | E-flite Viper 90mm, Platinum 150A V5.1 | Actual aircraft setup 333 Hz | Original servos, standard PWM |
| Copter | ArduCopter | Pixhawk4 | Blade 500 helicopter, Platinum 80A (validate exact V5 serial compatibility) | Configure per helicopter ESC | Standard until optional KST implementation is integrated and tested |

## Shared Hobbywing module status

- `libraries/AP_HobbywingV5` and `AP_BLHeli` Hobbywing V5 receive-only telemetry: present in the common codebase, gated by **SERVO_BLH_HWV5=0 by default**. No motor commands are sent over serial.
- The **KST 760µs/560Hz implementation is NOT yet merged** into this source revision. The product/family name describes the planned combination. KST configuration must not be advertised in Mission Planner as available until its isolated port, IOMCU firmware, host tests and hardware regression have passed. Stock PWM remains the default.
- Version names: `ArduPlane X.Y.Z(-dev)` and `ArduCopter X.Y.Z(-dev)` plus `Hobbywing V5.1 + KST 760µs/560Hz` family label. The core version remains unchanged and accurate.
- Normal operation sends ESC data via `AP_ESC_Telem` / MAVLink rather than unsolicited text. Set `SERVO_BLH_HWRAW=1` or `SERVO_BLH_HWPMON=1` only for diagnosis.
- These two .apj firmware files can be produced even though KST is not yet implemented. Do not confuse compilation with completed combined feature verification.

## Build both in one pass

```bash
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch integration/viper90-hobbywing-v5-kst
git pull --ff-only origin integration/viper90-hobbywing-v5-kst
source .venv/bin/activate
bash Tools/hobbywing_v5/build_dual_firmware.sh
```

The script executes Hobbywing host tests, builds **Pixhawk6C Plane** and **Pixhawk4 Copter**, and places the two .apj files, git SHA, individual firmware version strings, checksums and exact feature-status notes in `build/firmware-family/<git-SHA>/`. No hardware installation or aircraft parameter changes. It requires the user's normal WSL ArduPilot toolchain and does not assert a build has run until local output confirms this.

## Viper parameter guide (apply after flashing, save .param first)

| Parameter | Viper established value |
|---|---|
| `SERIAL1_PROTOCOL` | 16 |
| `SERIAL1_BAUD` | 115 |
| `SERVO_BLH_HWV5` | 1 |
| `SERVO_BLH_HWOUT` | 3 |
| `SERVO_BLH_HWRPM` | 1 |
| `SERVO_BLH_POLES` | 4 magnetic motor poles |
| `SERVO_BLH_HWGAIN` | 1.0 |
| `SERVO_BLH_HWCOFF` | 0.0 |
| `SERVO_BLH_HWPMIN` | 5.0 A PM current gate, NOT calibration |
| `SERVO_BLH_HWRAW` | 0 |
| `SERVO_BLH_HWPMON` | 0 |
| `SERVO3_FUNCTION` | 70 / throttle |
| `SERVO11_FUNCTION` | 150 / reverse RCIN11Scaled |
| ESC throttle PWM | operator-proven 333 Hz — preserve actual `RC_SPEED` / timer grouping from aircraft .param |

Battery failsafe, RC failsafe, RTL, GPS/EKF, servo travel, power monitoring, 6V servo rail and parallel-powered gear were confirmed by the operator. **Do not embed these operator settings as firmware defaults**. They belong in the actual Viper .param; no aircraft-specific configuration is applied automatically by build script.

Motor shaft RPM = ESC electrical RPM × 2 / magnetic motor pole count (Viper: divide by two). Primary power module remains authority for battery voltage/current and failsafes; published ESC current has a known discrepancy and must not replace PM data.

## Blade 500 heli parameter guide (configure after flashing; not a preset)

1. Use **ArduCopter** with `FRAME_CLASS=6` only for the appropriate traditional helicopter type as supported by the actual Copter version. Restore/review heli's own verified .param (not the Viper .param).
2. Check which UART receives the Hobbywing 80A Platinum ESC serial data; set `SERIALx_PROTOCOL=16` and `SERIALx_BAUD=115` if its telemetry protocol has been independently verified to use compatible 32-byte Platinum V5 frames. Set `SERVO_BLH_HWV5=1` only when supported.
3. Match `SERVO_BLH_HWOUT` to **the physical HeliRSC/ESC PWM output** (previous heli arrangement: SERVO6; do not assume until current hardware map is checked).
4. Set `SERVO_BLH_POLES` to the *heli motor's actual magnetic pole count* and `SERVO_BLH_HWRPM=1` only once verified. ESC publishes motor shaft RPM. Rotor headspeed needs gear ratio or independent Align Hall sensor. Governor configuration must use trusted rotor RPM.
5. Leave original, regular servo PWM settings on existing servos. **Do not set 760µs/560Hz values until KST code and servos are physically installed**. KST mode will eventually be enabled through explicit parameters, not per-board naming.
6. Apply and verify the heli's own battery/failsafes and rotor interlock after separate controlled hardware tests.

## Shipping gate

- [x] Source includes Hobbywing parser and both vehicle targets; default params unchanged.
- [x] One script prepares independent Plane/Copter build outputs using matching git SHA.
- [ ] Local host tests and **both target compiles** completed without errors.
- [ ] KST 760µs/560Hz port completed with parameter defaults preserving standard PWM.
- [ ] KST IOMCU timing/pulse range verified on Pixhawk4; no normal 1520µs pulses to 760µs servos.
- [ ] Plane/Copter release build based on actual *verified stable* corresponding upstream tags.
- [ ] Hardware after-flash regressions, separately for Viper and Blade 500.

This file describes a staged engineering deliverable, not a certified or flight-validated production release.
