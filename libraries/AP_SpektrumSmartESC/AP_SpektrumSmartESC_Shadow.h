/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Control.h"

// Read-only Plane snapshot. No transport, ownership ACK or ESC calibration.
class AP_SpektrumSmartESC_Shadow {
public:
    static bool plane_demand(uint16_t pwm, uint16_t minimum, uint16_t trim, uint16_t maximum,
                             bool reversed, bool bipolar, AP_SpektrumSmartESC_Control::Demand &d)
    {
        d.throttle_percent = 0;
        d.reverse_request = false;
        // Zero PWM and malformed/out-of-range settings are not motor-neutral evidence.
        if (minimum == 0 || maximum <= minimum || pwm < minimum || pwm > maximum ||
            (bipolar && (trim <= minimum || trim >= maximum))) {
            return false;
        }
        float value;
        if (bipolar) {
            value = pwm >= trim ? 100.0f * (pwm - trim) / (maximum - trim) :
                                  -100.0f * (trim - pwm) / (trim - minimum);
            if (reversed) {
                value = -value;
            }
        } else {
            value = 100.0f * (pwm - minimum) / (maximum - minimum);
            if (reversed) {
                value = 100.0f - value;
            }
        }
        d.throttle_percent = value;
        d.reverse_request = value < 0;
        // esc_ok and motor_stopped intentionally remain unqualified.
        return true;
    }
};
