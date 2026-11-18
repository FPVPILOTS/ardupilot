"""Read-only Hobbywing Platinum V5 32-byte ESC telemetry reference parser.

Based on field definitions in Rotorflight GPL firmware (see
libraries/AP_HobbywingV5/SOURCES.md). No serial writes, no ESC control.

This is a HOST-SIDE research aid, NOT an ArduPilot flight driver.
"""
from dataclasses import dataclass
import struct

FRAME_LENGTH = 32
FRAME_TYPE = 0x5C30
FUNCTION = 0x03


class InvalidFrame(ValueError):
    """Received bytes are not a valid V5 telemetry frame."""


@dataclass(frozen=True)
class Telemetry:
    throttle_percent: int
    fault_raw: int
    rpm_field_x10: int
    voltage_v: float
    current_a: float
    esc_temperature_c: int
    bec_temperature_c: int
    motor_temperature_c: int
    bec_voltage_v: float
    bec_current_a: float


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def decode_telemetry(frame: bytes) -> Telemetry:
    """Decode one complete frame; caller must handle serial stream resync."""
    if len(frame) != FRAME_LENGTH:
        raise InvalidFrame("expected exactly 32 bytes")
    if frame[0] not in (0xFD, 0xFE):
        raise InvalidFrame("bad signature")
    if frame[3] != FUNCTION or struct.unpack_from("<H", frame, 4)[0] != FRAME_TYPE:
        raise InvalidFrame("not a Hobbywing V5 telemetry frame")
    expected = struct.unpack_from("<H", frame, 30)[0]
    actual = crc16_modbus(frame[:30])
    if expected != actual:
        raise InvalidFrame("CRC16 MODBUS mismatch")
    return Telemetry(
        throttle_percent=frame[9],
        fault_raw=frame[12],
        rpm_field_x10=struct.unpack_from("<H", frame, 13)[0] * 10,
        voltage_v=struct.unpack_from("<H", frame, 15)[0] / 10.0,
        current_a=struct.unpack_from("<H", frame, 17)[0] / 10.0,
        esc_temperature_c=frame[19],
        bec_temperature_c=frame[20],
        motor_temperature_c=frame[21],
        bec_voltage_v=frame[22] / 10.0,
        bec_current_a=frame[23] / 10.0,
    )
