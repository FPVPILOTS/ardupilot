/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// Source-only, HAL-independent RC3 arming prerequisite for the EXPLICIT
// EarlyArmedBench commissioning target. This cannot arm a motor by itself.
// It prevents trusting an ArduPlane-disarmed SERVO3=zero output as evidence
// that the pilot's throttle stick is genuinely down.
class AP_SpektrumSmartESC_RCArm {
public:
    static constexpr uint16_t IDLE_MARGIN_US = 45;
    static constexpr uint32_t IDLE_HOLD_MS = 750;

    static bool idle(uint16_t pwm, uint16_t minimum, uint16_t maximum,
                     bool reversed)
    {
        // Unset/implausible RC endpoints, receiver failsafe/no-value, and
        // a raised transmitter throttle may NEVER qualify a motor arm.
        if (minimum < 800 || maximum > 2200 ||
            maximum <= minimum || (maximum - minimum) < 500 ||
            // Radio edge jitter may fall a few microseconds beyond
            // calibrated min/max (e.g. RC3=992 for MIN=1000).
            // Reject anything farther away than the IDLE margin;
            // low-stick eligibility is still checked independently.
            int32_t(pwm) < int32_t(minimum) - IDLE_MARGIN_US ||
            int32_t(pwm) > int32_t(maximum) + IDLE_MARGIN_US) {
            return false;
        }
        const uint16_t idle_pwm = reversed ? maximum : minimum;
        const int32_t difference = int32_t(pwm) - int32_t(idle_pwm);
        return difference >= -int32_t(IDLE_MARGIN_US) &&
            difference <= int32_t(IDLE_MARGIN_US);
    }

    static bool stable(uint32_t since_ms, uint32_t latest_ms, uint32_t now_ms)
    {
        return since_ms != 0 && uint32_t(now_ms - since_ms) >= IDLE_HOLD_MS &&
            uint32_t(now_ms - latest_ms) < 100U;
    }
};
