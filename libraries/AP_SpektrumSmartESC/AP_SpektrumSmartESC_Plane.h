/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Motor.h"
#include "AP_SpektrumSmartESC_Shadow.h"
#include <math.h>

// Transport-independent conversion of the final, cached Plane output.
// No RC input is accepted. Ground/stop/IO ownership evidence is never fabricated.
class AP_SpektrumSmartESC_Plane {
public:
    struct Snapshot {
        uint8_t servo_index = 255;
        uint16_t function = 0, pwm = 0, minimum = 0, trim = 0, maximum = 0;
        bool reversed = false, bipolar = false;
        bool armed = false, rc_ok = false, control_ok = false, failsafe = false;
        bool reverse_configured = false, reverse_selected = false, reverse_permitted = false;
    };
    static bool convert(const Snapshot &s, uint32_t now_ms, AP_SpektrumSmartESC_Motor::Demand &out)
    {
        out = {};
        if (s.servo_index != 2 || s.function != 70 || (s.reverse_selected && !s.reverse_configured)) {
            return false;
        }
        AP_SpektrumSmartESC_Control::Demand signed_demand;
        if (!AP_SpektrumSmartESC_Shadow::plane_demand(s.pwm, s.minimum, s.trim, s.maximum,
            s.reversed, s.bipolar, signed_demand) ||
            (signed_demand.throttle_percent < 0 && (!s.reverse_selected || !s.reverse_permitted)) ||
            (s.reverse_selected && (!s.bipolar || !s.reverse_permitted || signed_demand.throttle_percent > 0))) {
            // A selected RC reverse switch must not turn an autonomous positive
            // throttle demand into reverse thrust. Require agreement with the
            // final signed vehicle output; zero remains a valid transition.
            return false;
        }
        out.magnitude = fabsf(signed_demand.throttle_percent) * 0.01f;
        out.reverse = s.reverse_selected;
        out.reverse_permitted = s.reverse_configured && s.reverse_permitted;
        out.armed = s.armed;
        out.rc_ok = s.rc_ok;
        out.control_ok = s.control_ok;
        out.failsafe = s.failsafe;
        out.acquired_ms = now_ms;
        return true;
    }
};
