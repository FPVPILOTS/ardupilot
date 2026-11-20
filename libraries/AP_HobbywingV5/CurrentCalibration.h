/*
 * Receive-only Hobbywing V5 ESC current correction.
 * GPL-3.0-or-later. Do not use an unverified reference as ground truth.
 */
#pragma once
#include <math.h>
namespace HobbywingV5 {
// Optional minimum total battery current used only to qualify an ESC current
// for DISPLAY. This cannot restore ESC readings below the sensor's deadband.
// A zero threshold disables the additional gate.
inline bool current_above_pm_threshold(float min_pm_a, bool pm_valid, float pm_a)
{
    return !isfinite(min_pm_a) || min_pm_a <= 0.0f ||
        (pm_valid && isfinite(pm_a) && pm_a >= min_pm_a);
}

// ESC current is not reliable at very low load on some units.
// Preserve raw values, and never create artificial current at zero throttle.
inline float corrected_current(float raw_a, float gain, float offset_a, unsigned throttle_percent)
{
    if (throttle_percent == 0 || !isfinite(raw_a) || raw_a <= 0.0f ||
        !isfinite(gain) || gain <= 0.0f || !isfinite(offset_a)) {
        return 0.0f;
    }
    const float corrected = raw_a * gain + offset_a;
    if (!isfinite(corrected) || corrected <= 0.0f) {
        return 0.0f;
    }
    // Hard cap to avoid publishing nonsensical values from bad tuning.
    return corrected > 1000.0f ? 1000.0f : corrected;
}
} // namespace HobbywingV5
