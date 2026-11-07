/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Optional calibration calculations. No defaults or vehicle configuration are
// supplied for the actual Avian. Values are not published without calibration.
class AP_SpektrumSmartESC_Measurements {
public:
    struct Calibration {
        bool poles_verified = false;
        uint16_t motor_poles = 0;
        bool battery_input_current_verified = false;
        bool capacity_baseline_verified = false;
        float baseline_mah = 0; // Known at the first acquired sample, not boot time.
    };
    void reset(const Calibration &calibration);
    void update(const AP_SpektrumSmartESC::Telemetry &data, uint32_t sample_ms,
                uint32_t now_ms, uint32_t sample_sequence);
    bool rpm(float &result, uint32_t now_ms) const;
    bool capacity(float &result, uint32_t now_ms) const;
private:
    Calibration _calibration;
    bool _have_sequence = false;
    uint32_t _sequence = 0;
    bool _have_rpm = false;
    uint32_t _rpm_ms = 0;
    float _rpm = 0;
    bool _previous = false;
    bool _coverage = false;
    uint32_t _current_ms = 0;
    float _current = 0;
    float _mah = 0;
};
