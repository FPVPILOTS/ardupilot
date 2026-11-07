/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Measurements.h"
#include <math.h>

void AP_SpektrumSmartESC_Measurements::reset(const Calibration &calibration)
{
    _calibration = calibration;
    _have_sequence = _have_rpm = _previous = false;
    _coverage = calibration.battery_input_current_verified && calibration.capacity_baseline_verified &&
                isfinite(calibration.baseline_mah) && calibration.baseline_mah >= 0;
    _mah = _coverage ? calibration.baseline_mah : 0;
}

void AP_SpektrumSmartESC_Measurements::update(const AP_SpektrumSmartESC::Telemetry &data,
                                            uint32_t sample_ms, uint32_t now_ms, uint32_t sequence)
{
    if (uint32_t(now_ms - sample_ms) >= 50) {
        _have_rpm = _coverage = _previous = false;
        return;
    }
    if (_have_sequence && sequence == _sequence) {
        return;
    }
    _have_sequence = true;
    _sequence = sequence;
    _have_rpm = (data.valid & AP_SpektrumSmartESC::ERPM) && _calibration.poles_verified &&
                _calibration.motor_poles >= 2 && (_calibration.motor_poles & 1U) == 0 &&
                data.electrical_rpm <= 655340U;
    if (_have_rpm) {
        _rpm = float(data.electrical_rpm) / (_calibration.motor_poles / 2U);
        _rpm_ms = sample_ms;
    }
    if (!_calibration.battery_input_current_verified || !(data.valid & AP_SpektrumSmartESC::CURRENT) ||
        !isfinite(data.current) || data.current < 0) {
        _coverage = _previous = false;
        return;
    }
    if (_previous) {
        const uint32_t dt = sample_ms - _current_ms;
        if (dt >= 50) {
            _coverage = false; // Includes out-of-order samples. Never fill an unknown gap.
        } else if (_coverage) {
            _mah += (_current + data.current) * 0.5f * (dt / 3600.0f);
        }
    }
    _current = data.current;
    _current_ms = sample_ms;
    _previous = true;
}

bool AP_SpektrumSmartESC_Measurements::rpm(float &result, uint32_t now_ms) const
{
    if (!_have_rpm || uint32_t(now_ms - _rpm_ms) >= 50) {
        return false;
    }
    result = _rpm;
    return true;
}

bool AP_SpektrumSmartESC_Measurements::capacity(float &result, uint32_t now_ms) const
{
    if (!_coverage || !_previous || uint32_t(now_ms - _current_ms) >= 50) {
        return false;
    }
    result = _mah;
    return true;
}
