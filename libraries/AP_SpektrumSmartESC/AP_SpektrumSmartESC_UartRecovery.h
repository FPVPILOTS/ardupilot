/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

// Keep UART error counters cumulative for diagnostics. A *framing-only* event
// may be a transient during ESC power-up. It does not permanently poison a
// later, separately authorized, handshake attempt after the bus has settled.
// Every other error (overrun, parity, noise, overflow, mux or release failure)
// remains fail-closed until reboot.
class AP_SpektrumSmartESC_UartRecovery {
public:
    static constexpr uint32_t QUIET_MS = 500;

    void record_framing_only(uint32_t now_ms)
    {
        _framing_only_events++;
        _last_framing_ms = now_ms;
        _seen = true;
    }

    bool eligible(uint32_t total_error_events, uint32_t now_ms) const
    {
        if (total_error_events == 0) {
            return true;
        }
        return _seen && total_error_events == _framing_only_events &&
            uint32_t(now_ms - _last_framing_ms) >= QUIET_MS;
    }

private:
    uint32_t _framing_only_events = 0;
    uint32_t _last_framing_ms = 0;
    bool _seen = false;
};
