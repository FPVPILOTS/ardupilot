# Avian 130A / Pixhawk6C ArmedBench — automatic battery-on discovery

Version: 2026-11-13, opt-in `Pixhawk6C-SpktSmart-ArmedBench-IOInstall`.

## User goal

Single-battery startup: after normal first-time `SPKT_MOTOR=1` selection
and reboot, the Pixhawk must **automatically** attempt to establish its
dedicated 115200-baud SRXL2 FC0x30 -> ESC0x40 link on AUX7 without a USB
trigger, manual handshake command, manual restart or startup-order ritual.

**Autoconnect is NOT auto-arm.** Actual motor throttle and CH7 reverse
must not be permitted until real independent control and sensor proof.

## What changed

- Dedicated ArmedBench board opts in to the existing 50ms receive-first
  SRXL2 reader policy; the former 2000ms silent preparation prevented
  the native master from promptly requesting a just-powered ESC.
  This does not accelerate or change ordinary Pixhawk6C firmware.
- The already existing native disarmed/no-peer 4-second backoff remains.
  It repeatedly discovers after power-order changes, including an ESC
  that receives battery power after the flight controller.
- Dedicated ArmedBench cold IOMCU proof transient failures are now
  retried while truly DISARMED, without granting UART or motor control
  before a fresh and valid readback.
- `SPK29 HS ST... Q... FIRST...ms` reports the reader handshake state,
  true TC-completed handshake count and milliseconds from native session
  start until the first completed targeted transmission.
- Host tests verify the 50ms RX-first interval, valid FC0x30/ESC0x40
  handshake, CRC and no `0xCD`/ARM before any stopped sensor evidence.

## Validation expectations

With real battery, `SPK29 FIRST` should be around 50ms from the time
the native UART session starts; it is **not** measured from the ESC
battery or Pixhawk MCU reset. To prove that the ESC answers, **RX must
increase above zero** and `HS RP` must reach at least 1. A TX counter
alone never proves a physical reply. If `RX0` persists after the
shorter startup, use a logic analyzer to check AUX7 signal integrity,
TX-to-ESC timing, return waveform, ground reference, electrical level,
and the physical Avian SRXL2 interface. Do not fake a link in firmware.

All normal RC/GPS/arming checks remain. No unqualified reverse channel,
no physical IOMCU bypass, no PWM fallback, and no automatic motor motion.
