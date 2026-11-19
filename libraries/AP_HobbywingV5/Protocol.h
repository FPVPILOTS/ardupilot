/*
 * Hobbywing Platinum V5 telemetry research support for ArduPilot.
 * Copyright and license: GNU General Public License v3 or later,
 * consistent with the ArduPilot and Rotorflight projects.
 *
 * Protocol field layout and serial framing are informed by Rotorflight:
 * https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/main/sensors/esc_sensor.c
 * Check original copyrights and preserve attribution if copying upstream code.
 * Bench-only experimental implementation; not yet flight validated.
 */
#pragma once

// Hobbywing Platinum V5 telemetry frame parser.
// Reference: Rotorflight src/main/sensors/esc_sensor.c (pl5DecodeTeleFrame).
// Intentionally no ArduPilot/HAL dependency; decoding only. No writes.
#include <stddef.h>
#include <stdint.h>

namespace HobbywingV5 {

struct Sample {
    uint8_t throttle_percent;
    uint8_t fault_raw;
    uint32_t rpm_field_x10; // NOT confirmed as mechanical motor shaft RPM
    float voltage_v;
    float current_a;
    int16_t esc_temp_c;
    int16_t bec_temp_c;
    int16_t motor_temp_c;
    float bec_voltage_v;
    float bec_current_a;
};

static constexpr size_t TELEMETRY_FRAME_LENGTH = 32;
static constexpr uint16_t TELEMETRY_TYPE = 0x5C30;

inline uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xA001U : crc >> 1;
        }
    }
    return crc;
}

inline uint16_t read_u16_le(const uint8_t *data)
{
    return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

inline bool decode_telemetry(const uint8_t *data, size_t len, Sample &out)
{
    if (data == nullptr || len != TELEMETRY_FRAME_LENGTH ||
        (data[0] != 0xFD && data[0] != 0xFE) ||
        data[3] != 0x03 || read_u16_le(data + 4) != TELEMETRY_TYPE ||
        read_u16_le(data + 30) != crc16_modbus(data, 30)) {
        return false;
    }
    // Construct a complete local sample; bad packets leave caller's output intact.
    Sample sample{};
    sample.throttle_percent = data[9];
    sample.fault_raw = data[12];
    sample.rpm_field_x10 = uint32_t(read_u16_le(data + 13)) * 10U;
    sample.voltage_v = read_u16_le(data + 15) * 0.1f;
    sample.current_a = read_u16_le(data + 17) * 0.1f;
    sample.esc_temp_c = data[19];
    sample.bec_temp_c = data[20];
    sample.motor_temp_c = data[21];
    sample.bec_voltage_v = data[22] * 0.1f;
    sample.bec_current_a = data[23] * 0.1f;
    out = sample;
    return true;
}

} // namespace HobbywingV5
