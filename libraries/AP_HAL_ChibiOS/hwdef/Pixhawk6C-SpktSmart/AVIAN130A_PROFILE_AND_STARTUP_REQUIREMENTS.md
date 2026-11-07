# Spektrum Avian 130A: configuration evidence, calibration and boot readiness

Status: requirements and observed hardware evidence only, **not** a motor-enabled firmware release. Applies to Pixhawk6C-SpktSmart AUX7/PA0 UART4 HSLab, target Spektrum Avian 130A Smart ESC.

## Identity is not bus address

- Spektrum protocol device ID `0x40` establishes **Smart ESC-class device** on SRXL2, not the ESC's exact current rating, hardware revision, firmware revision, model or settings.
- Target hardware: Avian 130 A 3S–6S `SPMXAE1130`; this model must be verified against label and/or the actual connected Spektrum SmartLink/Programmer data, not inferred from `0x40`. Version A reported on physical unit; read hardware and firmware version from actual programmer if available.
- Record identity as `operator-confirmed`, `programmer-read` or `unverified`. No fabricated model-auto-detection or imaginary SRXL2 configuration-read protocol.
- Actual tests 2026-11-07: handshake `0x40` success and 488 CRC-valid `0x21` frames, zero `0xCD` and `0x80` frames. No telemetry and no motor control proven. Historical UART FE4 is not new FE after success in shown snapshot.

## Two calibrations that are not interchangeable

1. **ESC radio/throttle range calibration** from Spektrum Avian manual: high-throttle at power-up, low-throttle in prescribed interval, motor tone confirmation. This pertains to ESC-radio throttle calibration with conventional signal. It is NOT proof of SRXL2 digital channel endpoint values, and is **not an appropriate unqualified startup action** for this custom driver; never auto-send maximum throttle during boot.
2. **SRXL2 mapping/profile qualification**: validate Channel Data `0xCD` wire units, bit mask, zero-based indices, actual low/mid/high channel codes, neutral and reverse-select thresholds and ESC programming. Channel 1 is bit/index 0 for throttle; if ESC configured for Spektrum CH7, channel 7 is zero-based index 6. No assumed `0x8000` center = ESC idle; no arbitrary PWM microsecond conversion.

Current bench with disconnected motor phases cannot establish motor response or perform manual tone-based calibration. Any future powered motor/EDF calibration requires a separate reviewed physical-safety procedure and evidence.

## ESC settings to capture in Spektrum SmartLink SPMXCA200

| Setting | Why it matters | Required source |
|---|---|---|
| ESC label/model/current rating | prevent wrong profile | product label / programmer; `0x40` insufficient |
| Hardware version + firmware version | protocol/feature compatibility; reverse support | actual SmartLink About/Firmware Upgrade |
| Flight Mode | fixed-wing vs helicopter governor | SmartLink General |
| Brake Type | Disabled, Normal, Proportional or Reverse | SmartLink General |
| Reverse/channel | Reverse brake type + explicit dedicated channel, proposed CH7 | SmartLink General/Throttle Control |
| Throttle endpoints | digital channel byte values, neutral, arm behavior | documented SRXL2 decoding + independently qualified safe tests |
| BEC voltage | avoid unintended parallel power through RC lead | SmartLink Throttle Control |
| Battery cells and low voltage cutoff | 3S–6S configuration and safe cutoff | SmartLink General |
| Motor timing and freewheeling | correct ESC/motor application | SmartLink Throttle Control |
| Startup mode | Normal or Soft Start | SmartLink Throttle Control |
| Motor poles | distinguish electrical RPM from mechanical RPM | motor data (not ESC bus ID) |
| Motor direction | must be physically tested later | SmartLink and supervised standalone motor bench |

If upgrading ESC firmware, Spektrum states settings reset to defaults; save all settings and reverify before use.

## Automatic startup acceptance matrix — production driver requirement

1. FC powers first / ESC powers later -> auto-handshake while neutral and disarmed; connect without manual `LABACK` or `HS`.
2. ESC powers first / FC starts later -> observe existing bus, handshake only if legitimately master; no bus collision.
3. Simultaneous power-up -> bounded handshakes / no duplicate master.
4. ESC power disconnect/reconnect, FC reboot, UART noise, RC loss -> invalidate telemetry promptly, clear stale throttle/reverse, bounded recovery, no unsolicited arm or old command replay.
5. 30-minute stable link -> telemetry updates, bounded error counters, no sustained reconnect storm.

**Existing HSLab one-shot mode stays blocked from motor control.** Do not flip safety macros to 1 to 'finish' the integration. Separately qualify production firmware against evidence.

## Mission Planner operator visibility

Distinguish `hardware detected`, `0x40 class-ID observed`, `model operator confirmed`, `handshake peer CRC-validated`, `Control Data TX-complete`, `sensor 0x80 CRC validated`, `telemetry accepted and fresh`, `output permitted`. Show explicit reason codes `ESC_NOT_POWERED`, `PEER_TIMEOUT`, `MASTER_CONFLICT`, `UART_FE`, `PROFILE_NOT_CONFIRMED`, `THROTTLE_ENDPOINTS_UNKNOWN`, `REVERSE_CONFIG_UNVERIFIED`, `TELEM_STALE`, `ARMING_BLOCKED`. Avoid reporting `LINK DISCOVERED` as operational motor-ready.

## Primary sources
- Spektrum SRXL2 library, examples, and Rev K specification: https://github.com/SpektrumRC/SRXL2
- Spektrum Avian original manual: https://www.spektrumrc.com/ProdInfo/Files/Avian-Manual-EN.pdf
- Spektrum SmartLink programming and firmware instructions: https://www.spektrumrc.com/on/demandware.static/-/Sites-horizon-master/default/dw8b38e716/Manuals/Spektrum-Avian-SmartLink-Update-Instructions.pdf
- Product specs Avian 130A: https://www.spektrumrc.com/product/spektrum-accessories-avian-130-amp-brushless-smart-esc-3s---6s/SPMXAE1130.html

## Verified operator photos: actual ESC Programmer V2 readout (2026-11-07)

This is a **manual inspection of 18 uploaded photos**, not protocol-auto-discovery. Do not extrapolate ESC settings from the `0x40` SRXL2 address. The physical shell is labeled Spektrum Avian 130 Amp 3S–6S and Horizon Hobby; exact part revision must still be checked from the unit's product label/SmartLink PC app. The programmer displays `Horizon V2 PB-1.0.28`; ESC displays `ESC Software Ver 04.0.27`. These are different devices/versions.

| Menu | Programmer label | Value photographed | Operational impact |
|---:|---|---|---|
| 1 | Flight Mode | Fixed-Wing | Correct family for Viper 90 EDF |
| 2 | Brake Type | Reverse | Enables reversal *when separately authorized on assigned channel* |
| 3 | Brake Force | 0 | Manufacturer recommends level **7** when Reverse Brake Type is used; change only deliberately, then qualify reverse effect |
| 4 | Cutoff Type | Soft Cutoff | Only operates if low-voltage cutoff is enabled |
| 5 | LiPo Cells | Auto Calc | Optional fixed **6S** only if the actual flight pack is known to be 6S |
| 6 | Cutoff Volt | Disabled | **No ESC low-voltage cutoff**. Recommend selecting suitable per-cell cutoff (e.g. 3.4 V/cell per Spektrum's later documented default) and retaining independent voltage/consumption alarms |
| 7 | BEC Voltage | 6.0 V | BEC + was isolated in SRXL2 lab. Never parallel with FC's external 5V/servo power without power plan |
| 8 | Startup Time | Soft | Soft ramp |
| 9 | Timing | 15 degrees | Keep until motor-specific qualification; does not identify SRXL2 timing |
| 10 | Motor Rotate | CW | Confirm physical EDF airflow before running |
| 11 | Active FW | Disabled | Active freewheeling disabled |
| 12 | Gov Gain | Level 1 | Heli-mode only, not active Fixed-Wing governor |
| 13 | AR Time | 45 s | Heli-mode only |
| 14 | Restart Accel | 1.5 s | Heli-mode only |
| 15 | Thrust Rev | CH7 | Spektrum physical CH7 corresponds to zero-based SRXL2 channel index 6; digital endpoint values **remain unverified** |

**Viper factory equipment conditional**: E-flite's Viper 90mm EDF manual identifies ESC `SPMXAE1130A`, 3670–1950 Kv inrunner motor `SPMXAM1400` with **4 physical poles**, and recommended **6S 22.2 V 5000 mAh 30C**. Confirm the user's aircraft still has the stock motor and intended pack before treating pole-pairs=2 or fixed 6S as verified.

### Manufacturer distinctions & next operations

- Spektrum Avian ESC programming instructions explicitly recommend Brake Force **7** for Reverse Brake Type and explain that `Cutoff Volt Disabled` removes the ESC LVC protection. In this profile, Brake Force 0 is **not sufficient evidence** to say reverse cannot work; document and test its actual braking behavior.
- Reverse CH7 is a *programmer configuration*, **not** proof that our custom FC uses the proper SRXL2 digital forward/reverse thresholds. `0xCD` packets contain actual channel output; do not send an unvalidated value as a telemetry-only poll.
- The ESC firmware version `04.0.27` is photographed, **not** independently verified as the latest available version in 2026; no forced firmware update. Updates may reset all ESC settings.
- **No motor control is enabled in the existing HSLab firmware**. Build software-side automatic handshake/recovery tests for FC-first, ESC-first, simultaneous and brownout startup; do not bypass HSLab arming or controlled handshake authorizations to get a spinning motor.
- First operator actions: agree battery cell count and LVC policy, decide whether to follow Spektrum's Brake Force 7 reverse-mode recommendation, confirm source of CH7 reverse and negative throttle failsafe, then run motor-free UART/control simulations before a separately supervised EDF test.

Sources: Spektrum Avian ESC Programming Instructions (official):
https://www.spektrumrc.com/on/demandware.static/-/Sites-horizon-master/default/dw8b38e716/Manuals/Spektrum-Avian-SmartLink-Update-Instructions.pdf

E-flite Viper 90mm manufacturer manual:
https://www.e-fliterc.com/on/demandware.static/-/Sites-horizon-master/default/dwc79ce86f/Manuals/Smart%20Viper%2090mm%20EDF%20Manual_EN.pdf

