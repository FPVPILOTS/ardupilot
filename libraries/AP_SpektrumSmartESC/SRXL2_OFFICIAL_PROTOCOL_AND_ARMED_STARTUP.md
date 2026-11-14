# Spektrum SRXL2 / Avian Smart ESC — source-grounded integration

Updated 2026-11-09. This file separates the published wire contract
from observed Pixhawk6C / Avian 130A hardware and unverified assumptions.

## Primary vendor sources

- Spektrum official implementation:
  https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.c
  and https://github.com/SpektrumRC/SRXL2/blob/master/Source/spm_srxl.h
- Spektrum example FC IDs / priority:
  https://github.com/SpektrumRC/SRXL2/blob/master/Examples/spm_srxl_config.h
- Spektrum official ESC telemetry layout:
  https://github.com/SpektrumRC/SpektrumDocumentation/blob/master/Telemetry/spektrumTelemetrySensors.h
- Throttle channel / reverse channels:
  https://wiki.spektrumrc.com/spektrum/receiver-port-mapping-guide
- Avian Smart ESC programming:
  https://www.spektrumrc.com/on/demandware.static/-/Sites-horizon-master/default/dw8b38e716/Manuals/Spektrum-Avian-SmartLink-Update-Instructions.pdf

## Wire packet IDs (not guesses)

All SRXL2 packets start with 0xA6 and end with CRC16. Length includes
the complete packet.

- 0x21 = Handshake, 14 bytes:
  A6 21 0E SOURCE DEST PRIORITY BAUD INFO UID[4] CRC[2].
  Source/destination are SRXL2 device IDs, not RC channel indices.
  FC device type 0x3n; ESC device type 0x4n.
  For this *isolated FC master to ESC* bus, FC=0x30 / ESC=0x40 was
  physically observed and priority 10/baud 115200 accepted. The old
  production SOURCE=0x31, priority 20 referred to an FC connected as
  a slave to a different receiver topology.
- 0xCD = Control Data, variable length:
  A6 CD LEN CMD REPLY_ID RSSI FRAME_LOSSES[2] MASK[4]
  PACKED_16BIT_VALUES[2*N] CRC[2].
  Command 0x00 = normal channel; 0x01 = failsafe channels;
  0x02 = VTX; 0x03 = Forward Programming.
  REPLY_ID=0x40 asks the ESC to respond with telemetry; REPLY_ID=0
  requests no telemetry. The 32-bit channel MASK is packed in
  channel-index order, so CH1 = bit0 (0x01), CH7 = bit6 (0x40),
  CH1+CH7 = 0x41. A 0xCD packet with CH1 alone has length 16;
  CH1+CH7 has length 18. Lowest two bits of each channel value are
  reserved/zero, 32768 denotes the center.
- 0x80 = Telemetry, 22 bytes on this 16-byte ESC sensor payload:
  A6 80 16 DEST SENSOR_DATA[16] CRC[2].
  The DEST on actual bench telemetry was FC 0x30. The telemetry
  packet does *not* contain an independent source device ID.
  Sensor ID 0x20 denotes ESC. Big-endian ESC fields include
  electrical RPM in 10-RPM increments, ESC input volts in 0.01V,
  FET temperature in 0.1 C, motor current in 0.01A,
  BEC data, throttle in 0.5% increments, and power output in 0.5%.
  These are not the Pixhawk power-module readings.
- 0x41 = bind data; 0x50 = parameter; 0x55 = RSSI.

## Startup, shutdown, reconnect

Published Spektrum srxlRun() and srxlParsePacket() do not require
repeated 0x21 handshakes every 50ms during normal RUNNING. Their
50ms startup listening/restart rules are for discovery and recovery,
not an authenticated heartbeat after each motor command.

1. FC and ESC may power up in either order. The FC listens first and
   accepts an unsolicited **CRC-valid** 0x40->0x00 advertisement as
   startup evidence, NOT as completed targeted authentication.
2. FC master ID 0x30 initiates 0x21 toward ESC ID 0x40 and
   receives a correctly addressed 0x21 response.
3. FC transmits final 0x21 broadcast to 0xFF and changes to RUNNING.
   115200 baud was actually demonstrated by the current 6C board.
4. Once RUNNING, FC transmits 0xCD CH1=minimum and selects REPLY_ID
   0x40 for 0x80 data when needed; the independent sensor reply is
   only usable with fresh TC-correlated, CRC-valid frames on this
   isolated single-ESC bus. No new 0x21 every 50ms is required.
5. If the FC starts before the ESC, disarmed discovery can safely
   retry after timeout (4-second cooldown in current source).
   A malformed peer, competing master or hardware fault is not
   treated as a successful recovery. Never automatically restore
   nonzero demand or rearm after an armed failure.
6. If ELRS/RP4TD RC arrives late while DISARMED, do not latch a
   permanent RC fault. Never command throttle without fresh RC.
7. During ARMED operation, loss of RC, command freshness, valid
   ESC replies, UART/IO ownership or disarming still fails closed.

## Observed, not assumed

- The 2026-11-09 bench saw SRXL2 LINK_ESTABLISHED, native UART
  hardware completion, 0x80 candidate values eRPM0/THpct0
  after CH1=768 and IOMCU CRC 0x6DBBAD71.
- This supports the CH1-minimum candidate but does NOT independently
  qualify CH1-full=64768, CH7 forward/reverse polarity, the actual
  effect on a motor, or behavior after loss of ESC signal.
- The compiled TurnLab firmware is *permanently unarmable*.
  This source work improves the separate native motor architecture;
  do not confuse compiling TurnLab with enabling SPKT_MOTOR.
- Native production remains source-gated in
  AP_SpektrumSmartESC_ProductionProfile.h and board hwdef.dat.
  Do not describe an armed motor test as complete based on these
  source changes alone.

## Acceptance tests before allowing the EDF to rotate

- Static protocol vectors for every packet type, CRC and mask.
- 3 cold-start topologies: FC-first, ESC-first and simultaneous.
- Power removal/re-addition while DISARMED, including missing RC.
- Authenticated targeted handshake before motor traffic.
- Fresh, timestamped 0x80 response to our TC-completed zero demand.
- Bench confirmation of CH1 idle/full range and CH7 mapping using
  a secure test rig; no reverse while the direction remains unknown.
- Test independent supply disconnect, disarm, RC/telemetry loss,
  power brownout and no uncommanded throttle on recovery.
- ArduPilot normal arming: no automatic arming and no PWM fallback.


## 2026-11-14 — Rev K RUNNING recovery and telemetry routing

This commissioning revision handles two overlooked protocol cases:
- Rev K 7.2: a CRC-verified target ESC (0x40) spontaneously restarting
  with 0x21 dest=0x00 means the existing link is no longer authoritative.
  It is never treated as a heartbeat. A new session is allowed only
  by the disarmed-only normal ESC-loss recovery path; an armed motor
  session never restarts automatically.
- Rev K 7.6: 0x80 byte 3 is the RF telemetry destination, not the ESC
  identity. 0x00 permits non-RF 0x80 telemetry, subject to the SAME
  existing physical-reply timing, single-ESC and decode gates.
  0xFF requests a fresh handshake and invalidates previous link status.

First fault details now appear in SPK34:
EV1=failed observed frame, EV2=unexpected header/destination,
EV3=invalid ESC telemetry, EV4=peer restart, EV5=telemetry rehandshake.
The additional counters track ESC restarts, rehandshake requests and
no-RF telemetry. No new type is treated as proof of a safe motor
command, and malformed/wrong-destination packets are still rejected.

Rev K 7.7.1 channel masks retain omitted channel state: CH1-only
packets do NOT reset a previously set CH7 direction. We keep reverse
disabled until Avian 130A Ver A polarity and neutral dwell are qualified.
Rev K 7.7.2's failsafe packet type is already encoded/tested by the
separate ChannelData codec. Actual Avian failsafe response has not yet
been established, so no unconditional 0xCD failsafe or emergency
throttle output is enabled in real hardware.

400000 baud is specified by Spektrum but is not selected by this build:
the current physical receive backend polls a 16-byte FIFO at 1kHz
and has been bench validated at only 115200. Setting the negotiation
bit without a verified faster receive path risks loss of ESC control.

The separate EarlyNative board now reports motor_arming_ready() false
unconditionally. A successful discovery and zero-throttle telemetry
cannot authorize physical arming until a distinct, qualified production
firmware is implemented. Hardware testing remains motor-phases OFF.
