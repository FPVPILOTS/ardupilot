/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// Bench authorization is separate from protocol or electrical qualification.
// Enabling diagnostics latches an arming veto until the next boot.
class AP_SpektrumSmartESC_Bench {
public:
    void observe_enable(int8_t enable) { _latched |= enable != 0; }
    bool arming_blocked(int8_t enable) const { return _latched || enable != 0; }
    void update_silence(uint32_t now_ms, uint32_t bytes, uint32_t last_byte_ms, bool receiving)
    {
        if (!receiving) {
            _active = _quiet = false;
            return;
        }
        if (!_active) {
            _active = true;
            _last_activity = bytes != 0 ? last_byte_ms : now_ms;
            _bytes = bytes;
        }
        if (bytes != _bytes) {
            _bytes = bytes;
            _last_activity = last_byte_ms;
            _quiet = false;
        }
        if (!_quiet && uint32_t(now_ms - _last_activity) >= 50) {
            _timeouts++;
            _quiet = true;
        }
    }
    uint32_t timeouts() const { return _timeouts; }
private:
    bool _latched = false;
    bool _active = false;
    bool _quiet = false;
    uint32_t _bytes = 0;
    uint32_t _last_activity = 0;
    uint32_t _timeouts = 0;
};
