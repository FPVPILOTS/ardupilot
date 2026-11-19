# Hobbywing Platinum V5 current calibration (experimental)

This feature adjusts **displayed ESC current only**, not throttle, reverse, BATT current, or the ESC firmware.

## New ArduPilot parameters

- `SERVO_BLH_HWGAIN`: gain, default **1.0**
- `SERVO_BLH_HWCOFF`: offset in amperes, default **0.0**

Published current is `max(0, raw_ESC_A * gain + offset_A)` only for **positive raw current and nonzero ESC throttle**. For zero raw current, zero throttle, invalid readings, or impossible output, the helper publishes zero. Output is capped at 1000 A; this is a guardrail, not a valid ESC operating limit.

The original current from the decoded frame is left unchanged. Messages `HWV5 ESC I10=` report raw current in 0.1 A, and `HWV5 CUR raw10=... pub10=...` report raw and published values in 0.1 A.

## Measurement method

1. **Do not calibrate from throttle %**. The 150A ESC's internal current reading may have a low-load deadband or nonlinear error. Verify PM current scaling first against an external DC-rated meter, and identify which loads the PM includes.
2. Record **time-aligned** raw ESC current, PM current, and independent DC meter current with stable battery and stable RPM across at least 5-8 distinct loads. Compare with the independent meter as the reference, not an unverified PM.
3. Fit gain and offset only over the verified current range. Use a held-out point and inspect residuals; a linear model cannot correct zero-valued ESC current during real current draw.
4. Until independently validated, retain defaults `HWGAIN=1` and `HWCOFF=0`. The four earlier observations (2.9/6.4, 2.9/7.3, 4.6/10, 10.1/17.3 A ESC/PM) do **not** justify a full-range correction.
5. Treat ESC current and BAT current as different measurements: PM may include other loads and filtering. This feature intentionally does not alter ArduPilot battery failsafes, battery consumption estimation, or power-module calibration.

Run `bash Tools/hobbywing_v5/run_host_tests.sh` and `./waf configure --board Pixhawk6C && ./waf plane` before installation. Test reception/telemetry with motor disconnected first.

## Time-paired PM-versus-ESC diagnosis (2026-11-19)

Enable `SERVO_BLH_HWPMON=1` to compare raw Hobbywing current with primary battery monitor values. Default is 0 to avoid diagnostic traffic. This feature **never changes** motor control, ESC current correction, BATT currents, battery consumption, or failsafes.

For every valid ESC frame, the ArduPilot driver snapshots the primary battery monitor reading. Every five seconds it reports the most recent paired values if both are available and the last ESC frame is no older than 1000 ms:

- `HWPM I10=N ESC10=M D10=K`: primary PM current, raw ESC current, and PM-minus-ESC difference, each in **0.1 A**. Example: `I10=50 ESC10=10 D10=40` means 5.0 A / 1.0 A / difference 4.0 A.
- `HWPM V10=N ESCV10=M`: primary PM voltage and ESC voltage in **0.1 V**.
- `HWPM unavailable or stale`: do not compare data when the PM current is unhealthy/unavailable or no fresh ESC sample exists.

The power module measures **battery input current**. The Hobbywing reports its own internal motor/ESC current, and the BEC may contribute to input current depending on ESC design. A difference is not automatically a calibration error. A power-module-to-ESC ratio is not valid if the ESC is below its minimum measurable current. Evaluate paired samples with stable RPM and control input, across the useful range, and use an independent suitable DC ammeter to certify accuracy.

For the 150A Platinum V5.1, BEC sensor payload bytes 20, 22 and 23 have so far been **0xFF (unavailable)**; 6 V configured BEC output must not be misrepresented as a measured BEC voltage. Rotorflight parses these bytes, but does not cause this ESC to populate them.
