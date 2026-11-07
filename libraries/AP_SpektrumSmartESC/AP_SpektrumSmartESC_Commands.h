/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Simulation-only command construction. No UART or vehicle output hook.
class AP_SpektrumSmartESC_Commands {
public:
    struct Profile {
        bool throttle_verified = false;
        uint8_t throttle_channel = 0; // Zero based; not a SERVO output number.
        uint16_t zero = 0;
        uint16_t full = 0;
        bool reverse_verified = false;
        uint8_t reverse_channel = 0;
        uint16_t forward = 0;
        uint16_t reverse = 0;
        uint32_t direction_dwell_ms = 0;
    };
    struct Demand {
        float throttle = 0; // Normalized forward/reverse magnitude, 0..1.
        bool reverse = false;
        bool armed = false;
        bool rc_ok = false;
        bool esc_ok = false;
        bool failsafe = false;
        bool stopped = false;
    };
    explicit AP_SpektrumSmartESC_Commands(const Profile &profile) : _profile(profile) {}
    uint8_t prepare(const Demand &demand, uint32_t now_ms, uint8_t *packet, size_t capacity);
    void reset();
    bool direction_reverse() const { return _reverse; }
private:
    Profile _profile;
    bool _authorized = false;
    bool _reverse = false;
    bool _neutral = false;
    uint32_t _neutral_since_ms = 0;
};
