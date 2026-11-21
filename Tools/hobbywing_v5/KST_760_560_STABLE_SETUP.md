# ArduPilot 4.7.3 | Hobbywing V5.1 + KST 760us / 560Hz

## Exactly what is integrated

The official common 4.7.3 base and both **ArduPlane / Pixhawk6C** and **ArduCopter / Pixhawk4** targets are unchanged at their reported 4.7.3 version. The optional Hobbywing Platinum V5 serial receive-only telemetry was ported earlier.

**KST optional port**:
- `SRVx_MIN/TRIM/MAX` metadata supports pulse settings down to **500us**, while the original parameter default values (1100/1500/1900us) remain untouched.
- Traditional-helicopter swash parameter `H_SW_PWM_MODE` (and swash2 on dual-swashes) defaults to **0**, retaining the official ArduCopter 4.7.3 legacy 1000/2000us swash endpoints and output behavior.
- `H_SW_PWM_MODE=1` opts in to configured `SERVOx_MIN/TRIM/MAX` and reversal. Internally swash output is represented as a signed angle range so **zero output maps to SERVOx_TRIM**, including a 760us center. Default 0 does not change normal servo outputs.
- Optional **560Hz** update rate on the IOMCU first PWM group only. The special 560Hz mode is activated by an **explicit rate request**, not automatically just because the firmware supports it. Normal PWM still obeys the existing 400Hz cap. With 560Hz selected, IOMCU first-group nonzero pulse width is constrained to **560..960us**; zero-pulse failsafe is preserved.
- Firmware filenames keep the official version `4.7.3` and label the additional family features `Hobbywing V5.1 + KST 760us/560Hz`. The filename **does not** configure aircraft parameters.

## Build both from exactly one common source SHA

```bash
wsl -d Ubuntu
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch stable/ardupilot-4.7.3-hobbywing-kst
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
source .venv/bin/activate
bash Tools/hobbywing_v5/build_stable_pair.sh
```

This runs host parser tests plus KST source-invariant checks and compiles two .apj files; it does not flash or arm either vehicle. Record both SHA256 checksums. Build targets are Plane/Pixhawk6C and Copter/Pixhawk4.

## Viper 90mm, Pixhawk6C (original servos)

- Keep the **actual operator-tested** Viper .param export (333Hz Hobbywing throttle, SERVO3_FUNCTION=70, SERVO11_FUNCTION=150 with verified RC-failsafe scaling, RTL and battery fail-safe). Default firmware alone does not encode these.
- Hobbywing V5.1 telemetry: `SERIAL1_PROTOCOL=16`, `SERIAL1_BAUD=115`, `SERVO_BLH_HWV5=1`, `SERVO_BLH_HWOUT=3`, `SERVO_BLH_HWRPM=1`, `SERVO_BLH_POLES=4`, `SERVO_BLH_HWRAW=0`, `SERVO_BLH_HWPMON=0`.
- KST feature stays unused; leave conventional `SERVOx_MIN/TRIM/MAX` and servo output rate settings. **Do not set 560Hz for ordinary servo channels**.
- 6V servo rail and separate parallel gear supply are physical hardware provisions and are not firmware defaults.

## Blade 500, Pixhawk4 (future KST 760us swash)

**Do not apply to normal 1520us servos.** For 760us-compatible KST servo(s), after manufacturer pulse/rate compatibility is confirmed, arrange an isolated, motor-disconnected hardware test:
1. Use ArduCopter traditional helicopter settings and the **actual** assigned swash output functions `SERVO1_FUNCTION` etc. `H_SW_PWM_MODE=0` is the default, which forces the original 1000..2000us swash mapping.
2. For only 760us-compatible swash servos, configure `SERVOx_MIN=560`, `SERVOx_TRIM=760`, `SERVOx_MAX=960` and their physical reverse/endpoint behavior **before** setting `H_SW_PWM_MODE=1`; this option reconfigures how swash writes pulses and requires reboot and verification.
3. An explicit request for **560Hz** is separate from endpoint mapping. The special rate is only supported for the first IOMCU PWM group. When that group runs at 560Hz, it clamps all nonzero pulses to 560..960us; **do not share that group with ordinary 1520us servos, ESC or tail servos expecting different pulse widths**.
4. A normal-width tail servo must be assigned to an independent PWM timer group; verify Pixhawk4 MAIN/AUX actual routing and rail voltage. Keep throttle ESC on its own compatible group/rate; do not assume per-channel PWM frequency if multiple outputs share a hardware group.
5. Preserve zero-pulse IOMCU failsafe behavior. Verify output pulses, swash direction, servo power and failsafe physically with a scope/logic analyzer and motor disabled before moving to powered bench flight controls.
6. The **Hobbywing Platinum 80A** is not guaranteed to send the same compatible V5 32-byte UART telemetry as the confirmed 150A. Enable `SERVO_BLH_HWV5` for the heli only after checking the actual hardware protocol, serial pinout and signal voltage. Confirm motor pole count independently and use existing rotor headspeed sensor for heli governor.

## Release acceptance gates

- [ ] Both stable 4.7.3 targets compile on user's toolchain with the combined modifications.
- [ ] Standard servo behavior matches factory firmware with `H_SW_PWM_MODE=0`.
- [ ] KST opt-in 560/760/960us electrical pulse checks and true 560Hz PWM measured on Pixhawk4 IOMCU first group.
- [ ] ESC throttle, tail servo and their failsafes independently proven unaffected.
- [ ] Viper after-flash 333Hz throttle, reverse, RC failsafe and ESC telemetry regression.
- [ ] Only then mark this exact Git SHA as the validated release; preserve the previous known-good builds until acceptance.

These settings are installation guidance, **not** modifications to ArduPilot factory defaults.
