# Rotorflight Hobbywing Platinum V5 wire protocol: reference notes

Primary implementation: Rotorflight `src/main/sensors/esc_sensor.c`, functions `pl5Accept`, `pl5Decode`, `pl5DecodeTeleFrame`, `pl5SensorInit`. The notes below are derived from upstream code; they are **not** a verified capture from this particular 80A ESC.

## Serial and frame facts

- UART 115200 baud; verify 8N1 on hardware.
- PL5 response sync/signature: byte 0 is 0xFD or 0xFE (per `pl5Accept`).
- Telemetry frame: **32 bytes**, type **0x5C30** interpreted little-endian at bytes 4–5 (wire bytes `30 5C`), function byte 3 = `0x03`.
- Payload begins at byte **9**. Last 2 bytes are **CRC-16/MODBUS little-endian**, computed over bytes 0–29; reflected polynomial `0xA001`, initial value `0xFFFF`.
- Rotorflight uses a 5000 ms PL5 boot delay and 500 ms telemetry frame timeout. These are reference behavior, not necessarily optimal for ArduPilot.
- Parser must resynchronize, check exact length/type/function/CRC and reject malformed frames, stale data and unexpected replies.

## 32-byte telemetry layout (0-based absolute byte offset)

| Offset | Bytes | Name | Raw scaling | Units / caveat |
|---|---:|---|---|---|
| 0 | 1 | signature | raw | 0xFD or 0xFE |
| 1–2 | 2 | header | raw | preserve/validate as observed |
| 3 | 1 | function | raw | 0x03 |
| 4–5 | 2 | type | uint16 little-endian | 0x5C30 |
| 6–8 | 3 | header | raw | do not invent a meaning |
| 9 | 1 | throttle | 1 | percent |
| 10–11 | 2 | reserved | raw | reserved |
| 12 | 1 | fault | raw | bit/value meanings not proven |
| 13–14 | 2 | rpm | uint16 little-endian ×10 | Rotorflight calls result eRPM; actual motor pole conversion needs verification |
| 15–16 | 2 | battery voltage | uint16 little-endian ×0.1 | V |
| 17–18 | 2 | motor/battery current | uint16 little-endian ×0.1 | A |
| 19 | 1 | ESC temp | 1 | °C |
| 20 | 1 | BEC temp | 1 | °C; device may not report |
| 21 | 1 | motor temp | 1 | °C; device may not report |
| 22 | 1 | BEC voltage | ×0.1 | V; verify physical 80A reporting |
| 23 | 1 | BEC current | ×0.1 | A; verify physical 80A reporting |
| 24–29 | 6 | reserved | raw | reserved |
| 30–31 | 2 | CRC | uint16 little-endian | CRC16 MODBUS |

**Consumption (mAh) is not directly present in this 32-byte telemetry frame.** Rotorflight integrates consumption separately. ArduPilot must explicitly integrate validated current over time (if desired), carefully distinguish ESC current from BEC current, and handle staleness/reset/arming boundaries.

Rotorflight has a special case: at throttle=0 it sets motor current to 0 because some ESCs repeat the previous reading while stopping. Validate on actual hardware before adopting that rule; never conflate it with BEC output current.

## Other PL5 message types (research only, not to transmit in first driver)

- Ping response type 0x242C, 11 bytes.
- Device info response type 0x252C, 73 bytes.
- Parameter read response type 0x3835, 105 bytes.
- Parameter write and reset frames are specifically **out of scope** for a telemetry-only ArduPilot driver.
- Rotorflight supports passive telemetry as well as half-duplex parameter dialogues. A real boot capture must establish whether passive frames arrive from this ESC and with what timing; do not presume a handshake or send arbitrary requests.

## Acceptance / reject rules for implementation

1. No out-of-bounds access on any length.
2. CRC tested against independent reference and Rotorflight simulator vectors.
3. Accept only known telemetry type/function; do not accidentally parse a parameter response.
4. Check timeouts; expired values are **not valid zero readings**.
5. Consider plausible ranges only as diagnostics; avoid silently mutating valid readings.
6. Preserve raw frames for diagnostics without leaking personally identifiable details.
7. Verify endianness on both little- and big-endian host tests where possible.

## Rotorflight source reference

- https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/main/sensors/esc_sensor.c
- https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/utils/sitl/esc/pl5_esc.py
