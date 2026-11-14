# Avian 130A Ver A — ESC throttle setup & SRXL2 qualification

## Confirmed hardware
- Spektrum Avian 130A Smart ESC **SPMXAE1130A** fitted to E-flite
  Viper 90mm EDF. Previously reported firmware v04.0.27.
- TX16S -> RP4TD -> ArduPlane RC3 input is independently working.
- Dedicated Pixhawk6C UART4 on AUX7/PA0 using single-wire 115200.
- Shared-battery cold boot proved real 0x21 peer handshake and
  119 CRC-formatted CH1-only 0xCD controls with 118 valid 0x80
  ESC telemetry samples before 1 unusable 0x80 caused EV3.
- Last reported ESC sensor throttle was 0.0%, eRPM=0.
- RC input calibration does **not** qualify digitally encoded ESC CH1 values.

## Source requirements
Spektrum Avian manual, "ESC/Radio Calibration" p7 describes
the conventional procedure: high throttle at ESC battery connect,
two short acceptance beeps, low within 5 seconds, long done beep.
It does NOT explicitly state that this programming sequence accepts
SRXL2 (0x21/0xCD) frames instead of PWM control. **Do not attempt
full-throttle SRXL2 on power-on until manufacturer protocol behavior
has been verified**. Also, beep feedback comes from motor windings:
motor phase wires disconnected means no audible beeps; this cannot
be used as calibration confirmation.

Spektrum SPMXAE1130A product page confirms conventional stick
programming and SmartLink USB / programmer box. These are distinct
from the SRXL2 channel value mapping inside this custom Pixhawk
implementation.

For digital SRXL2 the command generator must know independently
verified zero/full values, correctly map the final SERVO3 Plane
throttle, maintain CH1 only and never send reverse CH7 unless
qualified. The existing profile's CH1=768..4608 window is a
**small unqualified protocol lab candidate**, not "full throttle".
SRXL2 Rev K published example gives 10912 for -100% and 32768 for
midpoint; this sample does NOT prove the exact Avian 130A stop/full
algorithm. Do not confuse a 1000-2000 us RC stick range with 16-bit
digital SRXL2 channel values.

## Separate EarlyArmedBench prototype
Build: Pixhawk6C-SpktSmart-EarlyArmedBench-IOInstall
- Starts same SRXL2 native UART4 driver just after BoardConfig,
  preserving the cold-battery auto-link behavior.
- The entire normal ArduPlane arming chain and physical IOMCU
  inhibitions remain mandatory.
- Unlike earlier ArmedBench, this image **does not allow
  monitor-only ARM before the ESC has actually linked and reported
  valid recent zero throttle / stopped eRPM**.
- Dedicated safety latch permits only experimental 0..6% CH1
  source values 768..4608, CH7 omitted, never PWM fallback.
- A CRC-valid foreign 0x80 sensor is ignored for control ACK:
  the last-good ESC telemetry watchdog still faults. Record SPK36.
- Not for flight. It is **not ready to flash or spin** until
  the 0x20 data interpretation, calibrated zero/full CH1, signal
  loss behavior, and independent hardware kill are verified.

## Correct qualification sequence, without moving EDF rotor
1. Read Spektrum product SmartLink settings to establish fixed-wing,
   brake mode, reverse assignment and actual firmware revision.
2. Isolate and disconnect all three EDF motor phase wires from
   ESC, cover individually. Preserve external independent ESC power
   cutoff, never rely solely on ARM switch software.
3. DISARMED boot/handshake: confirm 0x21/0xCD/0x80, SPK36 BAD
   sensor ID, valid stopped 0x20, no F12 or stale sensor failure.
4. Disarmed read-only RC3 and SERVO3: full low/high travel, RC link
   loss, ARM switch state, physical safety state. DO NOT transmit
   high throttle during cold boot as a substitute for PWM calibration.
5. Use a separately isolated test fixture / suitable non-EDF motor
   and manufacturer-approved analog PWM calibration *if needed*.
   Avoid power-on full-throttle with an assembled EDF/impeller.
6. Digital bench verification of recognized stop and the 0..100%
   throttle curve requires evidence from actual Avian sensor
   response and safe independently restrained/isolated motor load.
   Never claim 768/4608 maps to 0/6/100% from only bench mock data.
7. Only after proving loss-of-signal physical cutoff, disabled ->
   armed zero -> increasing throttle, and fresh disarm-required
   recovery may a separate production mode be considered.

Sources:
- https://www.spektrumrc.com/product/spektrum-accessories-avian-130-amp-brushless-smart-esc-ver-a-3s---6s/SPMXAE1130A.html
- https://www.spektrumrc.com/on/demandware.static/-/Sites-horizon-master/default/dwb627f043/Manuals/Avian-Manual-EN.pdf
- https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf
- https://github.com/ArduPilot/ardupilot_wiki/blob/master/common/source/docs/common-esc-calibration.rst
