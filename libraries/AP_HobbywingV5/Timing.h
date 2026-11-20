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
// Receive timing diagnostics only; no governor output or ESC commands.
#include <stdint.h>

namespace HobbywingV5 {
class Timing {
public:
    void mark_valid(uint32_t now_us)
    {
        if (seen_) {
            const uint32_t interval = now_us - last_us_; // wrap-safe unsigned subtraction
            if (interval > 0) {
                latest_interval_us_ = interval;
                if (interval > max_interval_us_) { max_interval_us_ = interval; }
                // EMA interval, 1/8 new data. Avoid floats in receive hot-path.
                ema_interval_us_ = ema_interval_us_ == 0 ?
                    interval : (uint32_t((uint64_t(ema_interval_us_) * 7U + interval) / 8U));
                intervals_++;
            }
        }
        seen_ = true;
        last_us_ = now_us;
        valid_++;
    }
    bool has_sample() const { return seen_; }
    uint32_t age_us(uint32_t now_us) const { return seen_ ? now_us - last_us_ : UINT32_MAX; }
    bool fresh(uint32_t now_us, uint32_t timeout_us = 500000U) const
    { return seen_ && age_us(now_us) <= timeout_us; }
    uint32_t interval_us() const { return latest_interval_us_; }
    uint32_t max_interval_us() const { return max_interval_us_; }
    uint32_t mean_interval_us() const { return ema_interval_us_; }
    uint32_t frequency_hz_x10() const
    { return ema_interval_us_ ? uint32_t(10000000ULL / ema_interval_us_) : 0U; }
    uint32_t count() const { return valid_; }
    uint32_t interval_count() const { return intervals_; }
private:
    bool seen_ = false;
    uint32_t last_us_ = 0;
    uint32_t latest_interval_us_ = 0;
    uint32_t max_interval_us_ = 0;
    uint32_t ema_interval_us_ = 0;
    uint32_t valid_ = 0;
    uint32_t intervals_ = 0;
};
} // namespace HobbywingV5
