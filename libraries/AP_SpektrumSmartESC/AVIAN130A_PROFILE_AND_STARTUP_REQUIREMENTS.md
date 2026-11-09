# Avian 130A Smart Ver A — Pixhawk 6C SRXL2 completion status

This page replaces the obsolete statement that the TurnLab profile returns
an empty struct: it now contains an **unqualified CH1 minimum candidate**,
which is usable only by one-shot `TURNRUN=2`. It is **not** a motor flight profile.

## Goal / architecture

TX16S → RadioMaster RP4TD → ArduPlane final SERVO3 throttle →
native UART4 on AUX7/PA0 → SRXL2 Channel Data CH1 → Spektrum Avian 130A.
The reverse demand is derived separately from ArduPlane's configured
reverse-throttle switch, then delivered in SRXL2 CH7. CH1/CH7 are **Spektrum
wire indices**, not physical Pixhawk SERVO1 or SERVO7 outputs. No PWM
controller or PWM fallback is part of this architecture.

## Physical evidence through 2026-11-09

- `0x21` handshake established to ESC ID `0x40`, Pixhawk ID `0x30`,
  priority 10, regular CRC-valid `0x21` peer announcements.
- Installed IOMCU image CRC **`0x6DBBAD71`** read back from the device.
  This is software identity; no independent physical MAIN3 pin isolation
  measurement has been supplied. Do not equate CRC with pin-level proof.
- `TURNRUN=2` sent true hardware-TC-completed `0xCD` frames with the
  candidate `CH1=768` and **no CH7 update**. The ESC produced correlated
  `0x80` telemetry candidates, including about 15.45V and eRPM=0.
  **Correction (22:15 bench log):** `THd100` means 10.0% reported sensor
  throttle, NOT zero. The previous diagnostic printed ten times the sensor
  percentage. CH1=768 is therefore *not proven* to be accepted idle.
  The source-less `0x80` frame is only a candidate, not authenticated ESC
  identity, and the SRXL2 sensor voltage is a different data path from
  the Pixhawk power-module battery monitor.
  This is useful protocol evidence, not proof that the ESC was armed or that
  a motor will stop after an unexpected control/link loss.
- The original zero-only trial aborted with `AGE P101` and `FAULT_SAFETY`.
  The zero-only candidate path now tolerates up to 200ms between genuine
  `0x21` announcements, while the forward-motor TurnLab remains at 100ms.
  The 22:15 run then aborted with `FAULT_TX_DENIED Q5 TC4`. The zero-only
  coordinator has since been modified to wait for the *native UART physical*
  2..100ms RX gap before consuming a frame, with defer and denial logging.
  The 2026-11-09 06:27 trial then passed native RX-gap checks (GAP DEFER0)
  and reached Q9/TC8 with eight time-window sensor responses showing
  eRPM=0 and THpct0, but aborted with AGE P219 IO11 ERR0 HF0. The
  authentic 0x21 peer timestamp exceeded the TurnLab 200ms deadline.
  The normal policy later reported LOST1/HANDSHAKING; no full three-second
  zero-only sequence has been proven yet.

  **2026-11-09 code patch, not yet locally compiled/flashed:** ONLY the
  unarmable CH1-minimum TurnLab permits a fresh, valid zero-throttle
  and zero-eRPM 0x80 sensor response (maximum one per real hardware TC,
  received within 12ms) to satisfy the temporary timing witness.
  AutoConnect's 300ms peer timeout is deferred solely with this witness
  and intact IO/disarm/UART interlocks, inside the immutable three-second
  window. The authenticated 0x21 peer timestamp is NEVER overwritten
  by a source-less 0x80. Lost/bad/nonzero replies and expired leases
  fail closed. Native motor operation and flight arming stay disabled.
- RP4TD raw RC3 input responded across 990..2011us, valid RC/failsafe
  `RC1 FS0`. `SERVO3_FUNCTION=70` was mapped (`MAP1`), but while the
  flight controller was disarmed, SERVO3 remained at 1100 and
  `STOP1 ARM0`. This is intentional.

## Separation of firmware modes

- **TurnLab**: permanently unarmable. `SPKT_TURNRUN=2` sends only a
  3-second CH1 candidate idle frame sequence, then stops. Forward
  `SPKT_TURNRUN=1` still requires the independent verified source profile.
- **Native production motor**: full 0..100% motor controller, CH7 reverse,
  signed ArduPlane SERVO3 adapter, direction dwell, link/RC/IO checks and
  telemetry support already exist in source.
- `SPKT_MOTOR=1` is an **explicit production selection** (reboot required,
  default zero). If selected without a source-reviewed physical ESC profile
  and hardware TX qualification, arming stays blocked. It does not enable
  throttle, disable safety checks, or fall back to PWM.
- The source-reviewed production settings are in
  `AP_SpektrumSmartESC_ProductionProfile.h`, currently deliberately empty.
  The board definitions retain
  `AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED=0` and
  `AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED=0`.
  Do not change these based on simulation, link status or CRC alone.

## Gates remaining before physical 6% and normal arming

1. Confirm correct Avian Ver A digital CH1 low and full endpoints from
   real hardware evidence (reference 768/64768 values are *candidates*).
2. Confirm **CH7 forward and reverse digital levels and polarity** on the
   actual ESC with Reverse still enabled. Only a calibrated independent
   reverse request may be acted on; raw RC7 itself never controls motor TX.
3. Independently check physical electrical compatibility/output ownership,
   actual stop on loss of UART/RC/FC signal, and independent power cutoff.
4. Establish genuine stopped/ground evidence; the native motor arming API
   requires it and will not synthesize such evidence from stick position.
5. Verify ESC-applied zero throttle, stopped eRPM and full failure behavior
   using a restrained, **rotorless** motor test. Do not test with a mounted
   90mm EDF rotor or a flight-ready aircraft.
   The native code now sends only CH1 zero while disarmed to bootstrap
   telemetry and requires a fresh zero-throttle/eRPM=0 observation before
   approving the motor for arming. A source-less 0x80 sensor frame cannot
   replace a live, separately validated ESC 0x21 handshake.
   The ArduPlane producer now supplies fresh `!is_flying()` ground evidence
   and the native receiver supplies sensor-derived stop evidence.
   Physical corroboration is still required before flight.
6. Only then enable hardware production flags on a separately reviewed
   target, set the physically measured production profile, run host/sanitizer
   and firmware builds, and bench-validate live full-range command, switch
   direction and all independent loss cases before flight.

## Production handshake detail still to validate

The physically proven AutoLab handshake uses flight controller ID `0x30`.
The existing native motor coordinator's `AP_SpektrumSmartESC_Reader::SOURCE`
is `0x31` (a valid Spektrum FC unit ID), which has **not** been verified
with this exact ESC on the bench. Do not treat the proven 0x30 exchange as
a demonstrated 0x31 motor-controller start. The real production handshake,
including every restart/failsafe scenario, remains an acceptance test.

## External references

- Spektrum SRXL2 protocol: https://github.com/SpektrumRC/SRXL2
- Spektrum SRXL2 receiver mapping (CH1 throttle, CH5-9 reverse):
  https://wiki.spektrumrc.com/spektrum/receiver-port-mapping-guide
- ArduPilot Plane reverse-thrust RCx_OPTION=64 setup:
  https://ardupilot.org/plane/docs/reverse-thrust-setup.html
- Avian safety and signal-loss guidance:
  https://www.spektrumrc.com/ProdInfo/Files/Avian-Manual-EN.pdf

No source-profile boolean, compile-time TX gate or arming status is to be
forced true merely to make the EDF spin.
