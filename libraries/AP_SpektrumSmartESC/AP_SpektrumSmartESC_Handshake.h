/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Offline packet planner only. No UART, GPIO or write callback. Not connected
// to vehicle diagnostics. FC-master compatibility with the actual ESC still
// requires verification; no control-data or throttle frames are constructed.
class AP_SpektrumSmartESC_Handshake {
public:
    enum class State : uint8_t { Disabled, Listen, Query, Wait, Confirm, Complete, Yielded, TimedOut };
    void reset(uint32_t now_ms, uint32_t uid);
    bool observe(const uint8_t *packet, size_t length, uint32_t now_ms);
    uint8_t prepare(uint32_t now_ms, uint8_t *packet, size_t capacity);
    State state() const { return _state; }
    uint8_t attempts() const { return _attempts; }
private:
    State _state = State::Disabled;
    uint32_t _since_ms = 0;
    uint32_t _uid = 0;
    uint8_t _attempts = 0;
};
