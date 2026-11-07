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
