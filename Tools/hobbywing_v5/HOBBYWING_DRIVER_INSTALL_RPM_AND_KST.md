# Hobbywing Platinum V5.1 telemetry in ArduPilot — installation and RPM

**Driver**: `libraries/AP_HobbywingV5/` (protocol parser, frame stream, sensor validation/current helpers) integrated by `libraries/AP_BLHeli/AP_BLHeli.cpp/.h`.
**Current built target**: Pixhawk6C with ArduPlane for Viper 90mm. **Not yet an upstream stock ArduPilot feature.** The full custom firmware must be compiled; setting parameters on unmodified stable firmware will not add this driver.

## 1. Architecture and wiring
- Hobbywing ESC throttle PWM remains a separate servo output (on Viper: SERVO3, confirmed 333 Hz). Reverse is a separate PWM channel (Viper: SERVO11), independently configured; **the telemetry UART does not generate or change either PWM**.
- ESC telemetry signal -> a 3.3 V-safe serial **RX** pin on the flight controller (Viper: Pixhawk6C SERIAL1 / TELEM1 logical port; confirm the physical port label vs ArduPilot SERIALx mapping on each board). ESC telemetry ground and FC ground must share reference. **Verify Hobbywing electrical signaling level and pinout before connection; do not assume TTL levels.** No ESC UART TX connection is needed because this driver is receive-only.
- Hobbywing 6 V BEC feeds the separately powered Pixhawk6C servo rail (as operator-installed); gear is powered separately in parallel. This does not power the FMU through the dedicated POWER input. Preserve the operator-verified electrical setup.
- Power module on pack input reports **whole-aircraft current and voltage** into battery monitor. Do not replace it with ESC current.

## 2. Build installation

```bash
cd ~/ardupilot-hobbywing-v5-pixhawk6c
git fetch origin
git switch release/viper90-hobbywing-v5-pixhawk6c-rc1
git pull --ff-only
source .venv/bin/activate
bash Tools/hobbywing_v5/build_viper_release.sh
```

Flash the `arduplane-Pixhawk6C-Viper90-rc1.apj` generated under `build/viper90-release/rc1-<SHA>/` using Mission Planner **Load custom firmware**. Save current working APJ and .param before flashing; confirm new boot SHA after. Build from local source; no prebuilt RC1 firmware is claimed here.

## 3. Viper parameter reference

| Parameter | Viper value | Meaning |
|---|---|---|
| `SERIAL1_PROTOCOL` | 16 | Dedicated ESC telemetry serial input |
| `SERIAL1_BAUD` | 115 | 115200 baud; set to match |
| `SERVO_BLH_HWV5` | 1 | Enable the custom receive-only V5 decoder |
| `SERVO_BLH_HWOUT` | 3 | Map ESC data to SERVO3 / `esc3_*` |
| `SERVO_BLH_HWRPM` | 1 | Enable mechanical shaft RPM publication |
| `SERVO_BLH_POLES` | 4 | Actual motor magnetic poles, not stator slots |
| `SERVO_BLH_HWGAIN` | 1.0 | Do not alter without independent ESC current calibration |
| `SERVO_BLH_HWCOFF` | 0.0 | Do not fabricate low-current load |
| `SERVO_BLH_HWPMIN` | 5.0 | Suppress displayed ESC amps below 5 A **battery PM** current (not a physical ESC sensor calibration) |
| `SERVO_BLH_HWPMON` | 0 | Paired PM/ESC engineering logs disabled in normal use |
| `SERVO_BLH_HWRAW` | 0 | Full raw-frame GCS logs disabled in normal use |

**SERVO3_FUNCTION = 70, SERVO11_FUNCTION = 150**, 333 Hz, reversals and RC-failsafe scaling are **airframe configuration**, not part of the Hobbywing UART driver. Preserve the verified Viper .param export. On a heli or a different flight controller, choose `SERVO_BLH_HWOUT` equal to the physical ESC throttle output, not the RC input number. `SERIALx` numbers vary across boards.

## 4. RPM: electrical vs mechanical vs helicopter rotor

Rotorflight's Platinum V5 decode represents telemetry RPM field as **electrical RPM** (our decoded `rpm_field_x10`). Our `HobbywingV5::mechanical_rpm` calculates:

```text
mechanical motor RPM = electrical RPM * 2 / magnetic motor poles
                     = electrical RPM / pole pairs
```

Viper example: `HWV5 RPMraw=65710` and **4 motor poles** -> `65710 * 2 / 4 = 32855 shaft RPM`. `SERVO_BLH_POLES=4` and `SERVO_BLH_HWRPM=1` publishes `esc3_rpm=~32855` in Mission Planner. Confirm manufacturer motor pole specification; optional independent optical tachometer for verification. Never confuse electrical RPM with shaft RPM; never set poles=2 simply to double displayed RPM.

For a traditional helicopter with a gearbox, **rotor headspeed RPM** is distinct:
```text
head RPM = motor shaft RPM / effective drive ratio
```
For example motor 16000 RPM and 10:1 drive ratio -> 1600 headspeed RPM. The ratio is from actual pinion/main gear/one-way configuration. Prefer an independently verified dedicated head-RPM sensor for rotor speed and governing. Do not invent a ratio or rely on ESC RPM alone for failsafe/governor until physical checks establish the correspondence.

## 5. Telemetry and troubleshooting

Mission Planner: Flight Data -> Status / User Items: `esc3_rpm`, `esc3_curr`, `esc3_volt`, `esc3_temp`. A working receiver shows `HWV5 RX ok=` increasing and age staying fresh. Receive-only UART frames are 32 bytes and CRC16-MODBUS verified; no ESC handshake is needed for live telemetry.

- Wrong/no RPM: inspect `SERVO_BLH_POLES`, `SERVO_BLH_HWRPM`, actual ESC output mapping, and `HWV5 RPMraw`. Throttle 0 means RPM may be zero.
- Missing telemetry: check serial port mapping, `SERIALx_PROTOCOL=16`, 115200 baud, signal + GND, electrical levels. Do not connect Hobbywing telemetry to an arbitrary unverified port.
- ESC amps disagree with primary PM: PM senses whole aircraft, ESC internal current can have low-load deadband and load-dependent error. Threshold only hides readings; **do not change battery-failsafe source**. `HWGAIN` and `HWCOFF` require independent validation; current no longer claimed calibrated.
- BEC value 255 is a non-reporting sentinel in observed frames, **not** 25.5 V/A or C. No live BEC sensor demonstrated for this ESC.

Research-only troubleshooting: set `SERVO_BLH_HWPMON=1` to inspect paired power-module measurements and/or `SERVO_BLH_HWRAW=1` to dump one complete CRC-approved 32-byte frame in four HEX chunks once per 5 s. Restore both to 0 when done.

## 6. Stable-base combination roadmap (KST + Hobbywing)

**Do not merge the old KST branch or the entire Viper development branch directly into stable.** They modify shared ArduPilot subsystems (especially RCOut/IOMCU and AP_BLHeli), may use different base commits and vehicle types. Use clean supported stable upstream tags; check version release status at https://github.com/ArduPilot/ardupilot/releases or https://firmware.ardupilot.org/ .

1. Create a fresh integration branch at the latest verified *Copter stable* tag for traditional helicopters (not the Plane stable tag). Record tag SHA. Keep Viper's release RC1 separately.
2. Port `AP_HobbywingV5/` pure parser, validity and host tests first. Then port the minimum AP_BLHeli hook and parameter declarations required, preserving existing standard ESC telem compatibility. Verify no duplicated parameter indices or name length > 16.
3. Rebase the KST 760 microsecond and 560 Hz PWM/IOMCU/RCOut enhancements as **separate, reviewable patches** onto the same stable Copter tag; audit AP_Servo, IOMCU and hardware target compatibility. Confirm servo pulse ranges, output timers and FMU/IOMCU routing with no normal-width PWM pulses on 760us servos.
4. Build Copter for the **actual helicopter controller (Pixhawk4)** with Hobbywing + KST, plus an unmodified control build. Add host tests, SITL, then pin/servo bench regression.
5. Keep the Hobbywing ESC data source separate from the mechanical rotor head RPM sensor; prove pole count, effective gearing, governor and external RPM before automatic throttle tests.
6. Ship two outputs independently: `Viper/Plane/Pixhawk6C` and `Heli/Copter/Pixhawk4`, both tagged with exact upstream tag and custom change SHA. No shared prebuilt firmware image can safely cover both vehicle variants.

Official source for serial telemetry setup: https://ardupilot.org/copter/docs/common-esc-telemetry.html
