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

    // DiagnosticRX::start_samples(4) is the normal reserved PA0 UART4
    // electrical monitoring mode after _begin(). It is not an operator
    // GPIO-probe request. A previous recovery check incorrectly required
    // mode==0, permanently excluding every healthy initialized native
    // UART4 session from the disarmed FE-only startup retry.
    static constexpr bool passive_uart_mode(uint8_t mode)
    {
        return mode == 0 || mode == 4;
    }

    // Strict decision used by the physical native adapter, not a generic
    // instruction to forgive bad UART traffic. A failed attempt has not
    // published a single completed frame or entered armed control.
    static bool allow_native_fe_startup_retry(bool ever_armed, bool armed,
                                              uint32_t tx_frames, uint32_t controls,
                                              uint8_t retries, bool framing_only)
    {
        return !ever_armed && !armed && tx_frames == 0 && controls == 0 &&
               retries < 2 && framing_only;
    }

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
