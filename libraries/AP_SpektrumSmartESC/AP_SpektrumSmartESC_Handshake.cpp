/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Handshake.h"

void AP_SpektrumSmartESC_Handshake::reset(uint32_t now_ms, uint32_t uid)
{
    _state = State::Listen;
    _since_ms = now_ms;
    _uid = uid;
    _attempts = 0;
}

bool AP_SpektrumSmartESC_Handshake::observe(const uint8_t *p, size_t n, uint32_t now_ms)
{
    if (p == nullptr || n != 14 || p[0] != 0xA6 || p[1] != 0x21 || p[2] != n ||
        AP_SpektrumSmartESC::crc16(p, n) != 0 || _state == State::Disabled) {
        return false;
    }
    // Never compete with an observed receiver/master. A fresh explicit reset
    // is required after yielding; silence is not permission to take over.
    if (p[3] >= 0x10 && p[3] <= 0x30) {
        _state = State::Yielded;
        return true;
    }
    if (p[3] != 0x40 || p[4] != 0x30 || p[5] < 1 || p[5] > 100 || p[6] > 1 ||
        _state != State::Wait || uint32_t(now_ms - _since_ms) >= 50) {
        return false;
    }
    _state = State::Confirm;
    return true;
}

uint8_t AP_SpektrumSmartESC_Handshake::prepare(uint32_t now_ms, uint8_t *packet, size_t capacity)
{
    if (_state == State::Listen && uint32_t(now_ms - _since_ms) >= 50) {
        _state = State::Query;
    }
    if (_state == State::Wait && uint32_t(now_ms - _since_ms) >= 50) {
        _state = _attempts < 3 ? State::Query : State::TimedOut;
    }
    if (_state != State::Query && _state != State::Confirm) {
        return 0;
    }
    const uint8_t payload[] = {0x30, uint8_t(_state == State::Query ? 0x40 : 0xFF),
                              20, 0, 0, uint8_t(_uid), uint8_t(_uid >> 8),
                              uint8_t(_uid >> 16), uint8_t(_uid >> 24)};
    const uint8_t n = AP_SpektrumSmartESC::encode(0x21, payload, sizeof(payload), packet, capacity);
    if (n != 0) {
        if (_state == State::Query) {
            _attempts++;
            _since_ms = now_ms;
            _state = State::Wait;
        } else {
            _state = State::Complete;
        }
    }
    return n;
}
