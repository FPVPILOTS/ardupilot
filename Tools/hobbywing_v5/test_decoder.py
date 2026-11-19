"""Host-only parser tests using SYNTHETIC packet fields; not real ESC captures."""
import struct
import unittest

from decoder import FRAME_LENGTH, InvalidFrame, crc16_modbus, decode_telemetry


def synthetic_frame():
    f = bytearray(FRAME_LENGTH)
    f[0] = 0xFD
    f[1] = 0x01
    f[3] = 0x03
    struct.pack_into("<H", f, 4, 0x5C30)
    f[9] = 65
    f[12] = 2
    struct.pack_into("<HHH", f, 13, 1200, 252, 315)
    f[19:24] = bytes([43, 34, 40, 84, 17])
    struct.pack_into("<H", f, 30, crc16_modbus(f[:30]))
    return bytes(f)


class TestHobbywingV5(unittest.TestCase):
    def test_crc_known_standard_vector(self):
        self.assertEqual(crc16_modbus(b"123456789"), 0x4B37)

    def test_decode_synthetic_frame(self):
        sample = decode_telemetry(synthetic_frame())
        self.assertEqual(sample.throttle_percent, 65)
        self.assertEqual(sample.fault_raw, 2)
        self.assertEqual(sample.rpm_field_x10, 12000)
        self.assertAlmostEqual(sample.voltage_v, 25.2)
        self.assertAlmostEqual(sample.current_a, 31.5)
        self.assertEqual(sample.esc_temperature_c, 43)
        self.assertEqual(sample.bec_temperature_c, 34)
        self.assertEqual(sample.motor_temperature_c, 40)
        self.assertAlmostEqual(sample.bec_voltage_v, 8.4)
        self.assertAlmostEqual(sample.bec_current_a, 1.7)

    def test_reject_short_and_long(self):
        data = synthetic_frame()
        for sample in (data[:-1], data + b"\0", b""):
            with self.assertRaises(InvalidFrame):
                decode_telemetry(sample)

    def test_reject_bad_crc(self):
        data = bytearray(synthetic_frame())
        data[15] ^= 0x01
        with self.assertRaises(InvalidFrame):
            decode_telemetry(bytes(data))

    def test_reject_wrong_signature_and_type(self):
        f = bytearray(synthetic_frame())
        f[0] = 0x00
        with self.assertRaises(InvalidFrame):
            decode_telemetry(bytes(f))
        f = bytearray(synthetic_frame())
        f[4] = 0x00
        struct.pack_into("<H", f, 30, crc16_modbus(f[:30]))
        with self.assertRaises(InvalidFrame):
            decode_telemetry(bytes(f))

    def test_accept_alternate_signature(self):
        f = bytearray(synthetic_frame())
        f[0] = 0xFE
        struct.pack_into("<H", f, 30, crc16_modbus(f[:30]))
        self.assertEqual(decode_telemetry(bytes(f)).throttle_percent, 65)


if __name__ == "__main__":
    unittest.main()
