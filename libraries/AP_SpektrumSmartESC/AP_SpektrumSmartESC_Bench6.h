/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <math.h>
#include <stdint.h>

// Host-testable safety contract for a future, separately reviewed SRXL2
// motor-on bench image. This class NEVER sends a packet or enables UART TX.
// "Fixture verified" is an operator assertion, not a physical measurement.
class AP_SpektrumSmartESC_Bench6 {
public:
    static constexpr float MAX_MAGNITUDE = 0.06f;
    static constexpr uint32_t DIRECTION_DWELL_MS = 500U;
    struct Input {
        float demand = 0.0f;
        bool reverse = false;
        bool operator_enabled = false;
        bool external_kill_ready = false;
        bool fixture_verified = false;
        bool control_fresh = false;
        bool link_fresh = false;
        bool stopped = false; // Fresh, independent stopped evidence.
        bool reverse_config_verified = false;
    };
    enum class Reason : uint8_t {
        ZERO, FORWARD, REVERSE, WAIT_NEUTRAL, WAIT_STOP,
        WAIT_DWELL, FAULT
    };
    struct Output {
        float magnitude = 0.0f;
        bool reverse = false;
        bool clipped = false;
        bool publishable = false; // No UART output unless explicitly true.
        Reason reason = Reason::ZERO;
    };

    Output evaluate(const Input &in, uint32_t now_ms)
    {
        if (_fault) {
            return fault_output();
        }
        if (!in.operator_enabled || !in.external_kill_ready ||
            !in.fixture_verified || !in.control_fresh || !in.link_fresh ||
            !isfinite(in.demand) || in.demand < 0.0f ||
            (in.reverse && !in.reverse_config_verified)) {
            _fault = true; // Fault remains latched for this instance.
            return fault_output();
        }

        if (!_enabled) {
            if (in.demand != 0.0f || in.reverse || !in.stopped) {
                return zero(Reason::WAIT_NEUTRAL);
            }
            _enabled = true; // Only zero + forward + stopped may initialize.
            return zero(Reason::ZERO, true);
        }

        if (in.reverse != _reverse) {
            // A direction request with nonzero demand NEVER changes direction.
            if (in.demand != 0.0f) {
                _transition = false;
                return zero(Reason::WAIT_NEUTRAL, true);
            }
            if (!in.stopped) {
                _transition = false;
                return zero(Reason::WAIT_STOP, true);
            }
            if (!_transition || _requested_reverse != in.reverse) {
                _transition = true;
                _requested_reverse = in.reverse;
                _transition_since = now_ms;
                return zero(Reason::WAIT_DWELL, true);
            }
            if (uint32_t(now_ms - _transition_since) < DIRECTION_DWELL_MS) {
                return zero(Reason::WAIT_DWELL);
            }
            _reverse = in.reverse;
            _transition = false;
            _post_change_hold = true;
            _post_change_since = now_ms;
            return zero(Reason::WAIT_DWELL);
        }

        _transition = false;
        if (_post_change_hold) {
            if (!in.stopped) {
                _post_change_since = now_ms;
                return zero(Reason::WAIT_STOP);
            }
            if (uint32_t(now_ms - _post_change_since) < DIRECTION_DWELL_MS) {
                return zero(Reason::WAIT_DWELL);
            }
            _post_change_hold = false;
        }

        Output result;
        result.magnitude = in.demand > MAX_MAGNITUDE ? MAX_MAGNITUDE : in.demand;
        result.clipped = in.demand > MAX_MAGNITUDE;
        result.reverse = _reverse;
        result.publishable = true;
        result.reason = _reverse ? Reason::REVERSE : Reason::FORWARD;
        return result;
    }

    bool faulted() const { return _fault; }
    bool enabled() const { return _enabled && !_fault; }

private:
    Output zero(Reason r, bool publishable = false) const
    {
        Output o;
        o.reason = r;
        o.reverse = _reverse; // Retain direction until neutral+dwell completes.
        o.publishable = publishable;
        return o;
    }
    Output fault_output() const { return zero(Reason::FAULT); }

    bool _enabled = false;
    bool _fault = false;
    bool _reverse = false;
    bool _transition = false;
    bool _requested_reverse = false;
    bool _post_change_hold = false;
    uint32_t _transition_since = 0;
    uint32_t _post_change_since = 0;
};
