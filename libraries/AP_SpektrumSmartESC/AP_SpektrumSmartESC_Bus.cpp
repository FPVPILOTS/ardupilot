/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Bus.h"

bool AP_SpektrumSmartESC_Bus::diagnostic_packet(const uint8_t *p, size_t n)
{
    if (p == nullptr || n != 14 || p[0] != 0xA6 || p[2] != n ||
        AP_SpektrumSmartESC::crc16(p, n) != 0) {
        return false;
    }
    if (p[1] == 0x21) {
        return p[3] == 0x30 && (p[4] == 0x40 || p[4] == 0xFF) &&
               p[5] == 20 && p[6] == 0 && p[7] == 0;
    }
    if (p[1] != 0xCD || p[3] != 0 || p[4] != 0x40) {
        return false;
    }
    for (uint8_t i = 5; i < 12; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

bool AP_SpektrumSmartESC_Bus::gated() const
{
    return (_port.simulated() || AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED) &&
           _gate.electrical_verified && _gate.controller_role_verified && _gate.esc_initialization_verified;
}

void AP_SpektrumSmartESC_Bus::start(uint32_t now_us, uint32_t uid)
{
    disable();
    if (_state == State::FAULT) {
        return;
    }
    if (!_port.acquire()) {
        _state = State::FAULT;
        return;
    }
    _owned = true;
    if (!_port.release_rx()) {
        _state = State::FAULT;
        return;
    }
    _uid = uid;
    _last_activity = _started = _phase_since = now_us;
    _handshake.reset(0, uid);
    _state = gated() ? State::BUS_IDLE : State::PASSIVE_RX;
}

void AP_SpektrumSmartESC_Bus::disable()
{
    _have_telemetry = false;
    _poll_interval = 0;
    _length = 0;
    if (_owned) {
        if (!_port.release_rx()) {
            _state = State::FAULT;
            return;
        }
        _port.relinquish();
        _owned = false;
    }
    _state = State::DISABLED;
}

void AP_SpektrumSmartESC_Bus::fail(State state)
{
    _have_telemetry = false;
    _poll_interval = 0;
    _length = 0;
    _state = _port.release_rx() ? state : State::FAULT;
}

void AP_SpektrumSmartESC_Bus::activity(uint32_t now_us)
{
    _last_activity = now_us;
    if (_state == State::TX_ACTIVE) {
        _counts.collisions++;
        fail(State::FAULT);
    }
}

void AP_SpektrumSmartESC_Bus::transmit(uint32_t now_us)
{
    if (!gated() || !_port.idle_high() || uint32_t(now_us - _last_activity) < 174) {
        return;
    }
    if (_port.collision() || !_port.begin_tx(_buffer, _length)) {
        _counts.collisions++;
        fail(State::FAULT);
        return;
    }
    _counts.tx++;
    if (_kind == Kind::Poll) {
        _last_poll = now_us; // Delayed bus access must not create catch-up bursts.
    }
    _phase_since = now_us;
    _state = State::TX_ACTIVE;
}

void AP_SpektrumSmartESC_Bus::update(uint32_t now_us)
{
    if (_state == State::DISABLED || _state == State::PASSIVE_RX || _state == State::FAULT ||
        _state == State::COMMUNICATION_LOST) {
        return;
    }
    if (!gated()) {
        fail(State::PASSIVE_RX);
        return;
    }
    if (_state == State::TX_ACTIVE) {
        if (_port.collision()) {
            _counts.collisions++;
            fail(State::FAULT);
        } else if (_port.transmission_complete()) {
            // Release before allowing a response to be accepted.
            if (!_port.release_rx()) {
                fail(State::FAULT);
                return;
            }
            _last_activity = _phase_since = now_us;
            _state = _kind == Kind::Confirm ? State::CONNECTED : State::WAITING_FOR_RESPONSE;
            if (_kind == Kind::Confirm) {
                _last_peer = now_us;
                _last_poll = now_us;
            }
        } else if (uint32_t(now_us - _phase_since) >= uint32_t(_length) * 87U + 1000U) {
            _counts.timeouts++;
            fail(State::FAULT);
        }
        return;
    }
    if (_state == State::CONNECTED) {
        if (uint32_t(now_us - _last_peer) >= 50000) {
            _counts.timeouts++;
            fail(State::COMMUNICATION_LOST);
        } else if (_poll_interval != 0 && uint32_t(now_us - _last_poll) >= _poll_interval) {
            queue_poll(now_us);
        }
        return;
    }
    if (_state == State::WAITING_FOR_RESPONSE) {
        const uint32_t timeout = _kind == Kind::Poll ? 5000 : 50000;
        if (uint32_t(now_us - _phase_since) < timeout) {
            return;
        }
        _counts.timeouts++;
        if (_kind == Kind::Poll || _handshake.attempts() >= 3) {
            fail(State::COMMUNICATION_LOST);
            return;
        }
        _state = State::BUS_IDLE;
    }
    if (_state == State::BUS_IDLE) {
        if (!_port.idle_high() || uint32_t(now_us - _last_activity) < 174) {
            if (uint32_t(now_us - _started) >= 200000) {
                _counts.timeouts++;
                fail(State::FAULT);
            }
            return;
        }
        const uint32_t elapsed_ms = uint32_t(now_us - _started) / 1000;
        _length = _handshake.prepare(elapsed_ms, _buffer, sizeof(_buffer));
        if (_length != 0) {
            _kind = Kind::Query;
            _phase_since = now_us;
            _state = State::HANDSHAKE_PENDING;
        }
    }
    if (_state == State::HANDSHAKE_PENDING) {
        if (uint32_t(now_us - _phase_since) >= 50000) {
            _counts.timeouts++;
            fail(State::COMMUNICATION_LOST);
            return;
        }
        transmit(now_us);
    }
}

bool AP_SpektrumSmartESC_Bus::observe(const uint8_t *p, size_t n, uint32_t now_us)
{
    const State prior = _state;
    activity(now_us);
    if (prior == State::TX_ACTIVE || prior == State::DISABLED || prior == State::FAULT ||
        prior == State::COMMUNICATION_LOST || p == nullptr || n < 5 || n > 80 || p[0] != 0xA6 || p[2] != n) {
        _counts.unexpected++;
        return false;
    }
    if (AP_SpektrumSmartESC::crc16(p, n) != 0) {
        _counts.crc++;
        return false;
    }
    if (p[1] == 0x21 && n == 14 && p[3] >= 0x10 && p[3] <= 0x30) {
        // Another master or duplicate controller ID: never compete/recover automatically.
        _counts.unexpected++;
        fail(State::PASSIVE_RX);
        return false;
    }
    if (_state == State::PASSIVE_RX) {
        return false;
    }
    if (p[1] == 0xCD || (p[1] == 0x21 && n == 14 && p[3] != 0x40)) {
        _counts.unexpected++;
        fail(State::PASSIVE_RX);
        return false;
    }
    if (p[1] == 0x21 && n == 14 && p[3] == 0x40 &&
        (_state == State::CONNECTED || (_state == State::WAITING_FOR_RESPONSE && _kind == Kind::Poll))) {
        const uint32_t uid = uint32_t(p[8]) | (uint32_t(p[9]) << 8) |
                             (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
        _counts.unexpected++;
        fail(uid == _esc_uid ? State::COMMUNICATION_LOST : State::FAULT);
        return false;
    }
    if (_state == State::WAITING_FOR_RESPONSE && _kind == Kind::Query &&
        _handshake.observe(p, n, uint32_t(now_us - _started) / 1000)) {
        _esc_uid = uint32_t(p[8]) | (uint32_t(p[9]) << 8) | (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
        _last_peer = now_us;
        _length = _handshake.prepare(uint32_t(now_us - _started) / 1000, _buffer, sizeof(_buffer));
        _kind = Kind::Confirm;
        _phase_since = now_us;
        _state = State::HANDSHAKE_PENDING;
        return true;
    }
    if (_state == State::WAITING_FOR_RESPONSE && _kind == Kind::Poll && n == 22 &&
        p[1] == 0x80 && p[3] == 0x30 && uint32_t(now_us - _phase_since) < 5000 &&
        AP_SpektrumSmartESC::decode_esc(p + 4, 16, _telemetry)) {
        _have_telemetry = true;
        _telemetry_time = _last_peer = now_us;
        _state = State::CONNECTED;
        _counts.telemetry++;
        return true;
    }
    _counts.unexpected++;
    return false;
}

bool AP_SpektrumSmartESC_Bus::request_telemetry(uint32_t now_us, bool empty_mask_semantics_verified)
{
    update(now_us);
    if (_state != State::CONNECTED || !gated() || !empty_mask_semantics_verified) {
        return false;
    }
    return queue_poll(now_us);
}

bool AP_SpektrumSmartESC_Bus::set_mock_poll_interval(uint32_t interval_us, bool verified)
{
    if (interval_us == 0) {
        _poll_interval = 0;
        return true;
    }
    if (!_port.simulated() || !gated() || !verified || interval_us < 10000 || interval_us > 25000) {
        return false;
    }
    _poll_interval = interval_us;
    return true;
}

bool AP_SpektrumSmartESC_Bus::queue_poll(uint32_t now_us)
{
    // Rev K: mask 0 is startup/fade, not a universally documented ESC poll.
    // The caller must separately verify this behavior; there are no channel values.
    const uint8_t payload[] = {0, 0x40, 0, 0, 0, 0, 0, 0, 0};
    _length = AP_SpektrumSmartESC::encode(0xCD, payload, sizeof(payload), _buffer, sizeof(_buffer));
    _kind = Kind::Poll;
    _last_poll = now_us;
    _phase_since = now_us;
    _state = State::HANDSHAKE_PENDING;
    transmit(now_us);
    return _state == State::TX_ACTIVE || _state == State::HANDSHAKE_PENDING;
}

bool AP_SpektrumSmartESC_Bus::telemetry(AP_SpektrumSmartESC::Telemetry &result, uint32_t now_us) const
{
    if (!_have_telemetry || _state != State::CONNECTED || uint32_t(now_us - _telemetry_time) >= 50000) {
        return false;
    }
    result = _telemetry;
    return true;
}
