/*
 * Hobbywing Platinum V5 experimental telemetry field validation.
 * GPL-3.0-or-later; receive-only, no ESC control.
 */
#pragma once
#include <stdint.h>

namespace HobbywingV5 {

// 0xFF is an unavailable 8-bit sensor value in the observed 150A V5.1
// trace. Do not publish it as 255 C, 25.5 V or 25.5 A.
inline bool valid_byte_sensor(uint8_t value) { return value != 0xFFU; }
inline bool valid_word_sensor(uint16_t value) { return value != 0xFFFFU; }

// Rotorflight PL5 represents tele->rpm * 10 as electrical RPM.
// Mechanical shaft RPM requires the ACTUAL physical number of motor poles.
inline float mechanical_rpm(uint32_t electrical_rpm, uint8_t motor_poles)
{
    return motor_poles >= 2 ? (float(electrical_rpm) * 2.0f / float(motor_poles)) : 0.0f;
}

} // namespace HobbywingV5
