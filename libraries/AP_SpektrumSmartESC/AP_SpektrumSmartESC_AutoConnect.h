/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

// Phase 17: automatic SRXL2 startup/recovery *policy*.
// This class deliberately owns NO UART, timer, GPIO, RC, arming, or motor API.
// BEGIN_HANDSHAKE requests use of the already qualified bounded handshake
// transaction; it never authorizes sending 0xCD, throttle, or reverse.
//
// A CRC-valid 0x21 advertisement is not proof of a telemetry/control link.
// Linked means the dedicated peer completed a targeted handshake; any motor
// readiness must be independently qualified by the control layer.
class AP_SpektrumSmartESC_AutoConnect {
public:
    enum class State : uint8_t {
        OFF, LISTEN, HANDSHAKE, LINKED, BACKOFF, YIELDED, FAULT
    };
    enum class Reason : uint8_t {
        NONE, DISABLED, NOT_SAFE, PEER_TIMEOUT, HANDSHAKE_TIMEOUT,
        BAD_PEER, UART_ERROR, MASTER_CONFLICT, TRANSPORT_ERROR
    };
    enum class Action : uint8_t { NONE, BEGIN_HANDSHAKE };

    struct Status {
        State state = State::OFF;
        Reason reason = Reason::DISABLED;
        uint32_t attempts = 0;
        uint32_t successful = 0;
        uint32_t lost = 0;
        uint32_t rejected = 0;
        uint32_t last_peer_ms = 0;
        uint8_t peer_id = 0;
        bool device_id_matched = false; // only the bus ID can be checked here
    };

    // 1000 ms listen-before-talk; 7.5 s bounded handshake; 300 ms heartbeat
    // loss; 1/2/4s retries, capped at 3 consecutive failures + 10s cooldown.
    static constexpr uint32_t STARTUP_LISTEN_MS = 1000;
    static constexpr uint32_t HANDSHAKE_DEADLINE_MS = 7500;
    static constexpr uint32_t PEER_TIMEOUT_MS = 300;
    static constexpr uint32_t BACKOFF_BASE_MS = 1000;
    static constexpr uint32_t BACKOFF_CAP_MS = 4000;
    static constexpr uint32_t FAILURE_COOLDOWN_MS = 10000;

    void enable(uint32_t now_ms, uint8_t expected_id = 0x40)
    {
        _status = {};
        _status.state = State::LISTEN;
        _status.reason = Reason::NONE;
        _expected_id = expected_id;
        _state_since_ms = now_ms;
        _consecutive_failures = 0;
    }

    void disable()
    {
        _status = {};
        _status.reason = Reason::DISABLED;
        _expected_id = 0;
        _consecutive_failures = 0;
    }

    Action update(uint32_t now_ms, bool disarmed_bench_safe, bool bus_idle_high)
    {
        if (_status.state == State::OFF || _status.state == State::YIELDED ||
            _status.state == State::FAULT) {
            return Action::NONE;
        }
        if (!disarmed_bench_safe) {
            // Losing the external safety condition revokes any pending request
            // and cannot be re-authorized by a later update().
            _status.state = State::FAULT;
            _status.reason = Reason::NOT_SAFE;
            _status.device_id_matched = false;
            return Action::NONE;
        }
        if (_status.state == State::HANDSHAKE &&
            age(now_ms, _state_since_ms) >= HANDSHAKE_DEADLINE_MS) {
            fail(now_ms, Reason::HANDSHAKE_TIMEOUT);
        }
        if (_status.state == State::LINKED &&
            age(now_ms, _status.last_peer_ms) >= PEER_TIMEOUT_MS) {
            _status.lost++;
            fail(now_ms, Reason::PEER_TIMEOUT);
        }
        if (_status.state == State::BACKOFF &&
            age(now_ms, _state_since_ms) >= backoff_ms()) {
            _status.state = State::LISTEN;
            _state_since_ms = now_ms;
        }
        if (_status.state != State::LISTEN ||
            age(now_ms, _state_since_ms) < STARTUP_LISTEN_MS ||
            !bus_idle_high || _expected_id < 0x40 || _expected_id > 0x4F) {
            return Action::NONE;
        }
        _status.state = State::HANDSHAKE;
        _status.reason = Reason::NONE;
        _state_since_ms = now_ms;
        _status.attempts++;
        return Action::BEGIN_HANDSHAKE;
    }

    // Call only when the existing reader reports a CRC-valid, addressed,
    // matching peer response, actual UART TC and RX-restoration.
    bool handshake_complete(uint32_t now_ms, uint8_t peer_id,
                            bool valid_targeted_reply, bool uart_rx_restored)
    {
        if (_status.state != State::HANDSHAKE) {
            return false;
        }
        if (!valid_targeted_reply || !uart_rx_restored || peer_id != _expected_id) {
            fail(now_ms, Reason::BAD_PEER);
            return false;
        }
        _status.state = State::LINKED;
        _status.reason = Reason::NONE;
        _status.peer_id = peer_id;
        _status.device_id_matched = true; // ID matched; NOT model/firmware verified
        _status.last_peer_ms = now_ms;
        _status.successful++;
        _consecutive_failures = 0;
        return true;
    }

    // For an observed CRC-valid bus frame from the known peer; do not call
    // for just raw bytes, other IDs, an echo, or a CRC-invalid packet.
    bool peer_seen(uint32_t now_ms, uint8_t peer_id, bool crc_valid)
    {
        if (_status.state != State::LINKED || !crc_valid || peer_id != _expected_id) {
            return false;
        }
        // A late callback must not resurrect a stale link after a scheduler
        // stall. Force a fresh handshake rather than reusing old controls.
        if (age(now_ms, _status.last_peer_ms) >= PEER_TIMEOUT_MS) {
            _status.lost++;
            fail(now_ms, Reason::PEER_TIMEOUT);
            return false;
        }
        _status.last_peer_ms = now_ms;
        return true;
    }

    void handshake_failed(uint32_t now_ms)
    {
        if (_status.state == State::HANDSHAKE) {
            fail(now_ms, Reason::HANDSHAKE_TIMEOUT);
        }
    }

    // Once another master is seen, automatic transmit must NOT compete with
    // it. Only an explicit disable/enable reset can re-enter LISTEN.
    void competing_master()
    {
        if (_status.state != State::OFF) {
            _status.state = State::YIELDED;
            _status.reason = Reason::MASTER_CONFLICT;
            _status.device_id_matched = false;
        }
    }

    void uart_fault()
    {
        if (_status.state != State::OFF) {
            _status.state = State::FAULT;
            _status.reason = Reason::UART_ERROR;
            _status.device_id_matched = false;
        }
    }

    const Status &status() const { return _status; }

private:
    static uint32_t age(uint32_t now_ms, uint32_t previous_ms)
    {
        return now_ms - previous_ms; // unsigned wraparound-safe within short windows
    }

    uint32_t backoff_ms() const
    {
        if (_consecutive_failures >= 3) {
            return FAILURE_COOLDOWN_MS;
        }
        const uint32_t delay = BACKOFF_BASE_MS << (_consecutive_failures - 1);
        return delay < BACKOFF_CAP_MS ? delay : BACKOFF_CAP_MS;
    }

    void fail(uint32_t now_ms, Reason why)
    {
        _status.state = State::BACKOFF;
        _status.reason = why;
        _status.device_id_matched = false;
        _status.peer_id = 0;
        _status.rejected++;
        _state_since_ms = now_ms;
        if (_consecutive_failures < 3) {
            _consecutive_failures++;
        }
    }

    Status _status {};
    uint32_t _state_since_ms = 0;
    uint8_t _expected_id = 0;
    uint8_t _consecutive_failures = 0;
};
