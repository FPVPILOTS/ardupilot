/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// Parameter-independent volatile authorization; no GPIO/HAL writes.
class AP_SpektrumSmartESC_Probe {
public:
    // 0 disabled; 1 floating input; 2 weak pull-up; 3 weak pull-down; 4 UART RX.
    bool request(int8_t mode, int8_t legacy_enable, bool disarmed, uint32_t now_ms)
    {
        if (!disarmed || mode < 0 || mode > 4 || (mode != 0 && legacy_enable != 0)) {
            cancel();
            return false;
        }
        if (mode == 0) {
            _active = 0;
            _ready = true; // Persisted nonzero modes cannot self-authorize at boot.
            return true;
        }
        if (!_ready) {
            return false;
        }
        if (_active == 0) {
            _started = now_ms;
        }
        if (uint32_t(now_ms - _started) >= 60000) {
            cancel();
            return false;
        }
        _active = mode; // Changing probes does not extend the 60-second authorization.
        return true;
    }
    void cancel() { _active = 0; _ready = false; }
    uint8_t active() const { return _active; }
private:
    bool _ready = false;
    uint8_t _active = 0;
    uint32_t _started = 0;
};
