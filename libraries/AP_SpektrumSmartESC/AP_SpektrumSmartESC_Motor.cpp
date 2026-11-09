/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Motor.h"
#include <math.h>

bool AP_SpektrumSmartESC_Motor::control_packet(const uint8_t *p, uint8_t n)
{
    return p != nullptr && n == 18 && p[0] == 0xA6 && p[1] == 0xCD && p[2] == n &&
        p[3] == 0 && p[4] >= 0x40 && p[4] <= 0x4F && p[5] == 0 && p[6] == 0 && p[7] == 0 &&
        p[8] == 0x41 && p[9] == 0 && p[10] == 0 && p[11] == 0 &&
        (p[12] & 3U) == 0 && (p[14] & 3U) == 0 && AP_SpektrumSmartESC::crc16(p, n) == 0;
}

AP_SpektrumSmartESC_Motor::AP_SpektrumSmartESC_Motor(AP_SpektrumSmartESC_Bus::Port &port, Owner &owner,
    const AP_SpektrumSmartESC_Commands::Profile &profile) : _port(port), _owner(owner), _profile(profile)
{}

void AP_SpektrumSmartESC_Motor::disable()
{
    if (_leased) {
        const bool released = _port.release_rx();
        _port.relinquish();
        _owner.release();
        if (!released) {
            _status.fault = Fault::TURNAROUND;
        }
    }
    _leased = _tx = _waiting_reply = _have_telemetry = false;
    _reader.stop();
    _status.state = _status.fault == Fault::NONE ? State::DISABLED : State::FAULT;
}

void AP_SpektrumSmartESC_Motor::fault(Fault reason)
{
    if (_status.fault == Fault::NONE) {
        _status.fault = reason;
    }
    disable();
}

bool AP_SpektrumSmartESC_Motor::owned(uint32_t now_ms) const
{
    const auto r = _owner.receipt();
    return r.epoch == _epoch && r.io_boot == _io_boot && r.io_boot != 0 &&
        uint32_t(now_ms - r.acquired_ms) < 50 && r.main3_inhibited &&
        r.failsafe_inhibited && r.aux7_reserved;
}

bool AP_SpektrumSmartESC_Motor::start(const Qualification &q, bool disarmed, uint8_t target,
                                     uint32_t uid, uint32_t now_us, uint32_t now_ms)
{
    disable();
    // Explicit disarmed start is the ONLY way to clear a latched session fault.
    if (!disarmed || _status.fault == Fault::TURNAROUND) {
        return false;
    }
    _status = {};
    _status.simulated = _port.simulated();
    _qualification = q;
    if ((!_port.simulated() && (!physical_control_available() || !_profile.reverse_verified || !q.reverse_settings)) ||
        !q.electrical || !q.protocol || !q.endpoints || !q.esc_loss_behavior ||
        !_profile.throttle_verified || _profile.throttle_channel != 0 ||
        _profile.zero >= _profile.full || _profile.full > 65532U ||
        ((_profile.zero | _profile.full) & 3U) != 0 ||
        (_profile.reverse_verified && (!q.reverse_settings || _profile.reverse_channel != 6 ||
         _profile.direction_dwell_ms == 0 || _profile.forward == _profile.reverse ||
         _profile.forward > 65532U || _profile.reverse > 65532U ||
         ((_profile.forward | _profile.reverse) & 3U) != 0))) {
        fault(Fault::QUALIFICATION);
        return false;
    }
    if (++_epoch == 0) {
        ++_epoch;
    }
    if (!_owner.acquire(_epoch)) {
        fault(Fault::OWNERSHIP);
        return false;
    }
    _io_boot = _owner.receipt().io_boot;
    if (!owned(now_ms) || !_port.acquire()) {
        _owner.release();
        fault(Fault::OWNERSHIP);
        return false;
    }
    _leased = true;
    if (!_port.release_rx()) {
        fault(Fault::TURNAROUND);
        return false;
    }
    // Real isolated ESC bus uses the Spektrum FC-master/power-on profile:
    // 0x30 priority 10, startup announcement capture, then directed query.
    // Unlike the old 0x31 remote-receiver profile, this matches the
    // actual Pixhawk6C/Avian handshake already observed on the bench.
    const bool native_fc_master = !_port.simulated();
    if (!_reader.start(true, true, target, uid, now_us, native_fc_master)) {
        fault(Fault::PROTOCOL);
        return false;
    }
    _source = native_fc_id(_port.simulated());
    _parser = AP_SpektrumSmartESC(_source, target, 50, false);
    _target = target;
    _initial_idle = _transition = _neutral = _direction_changed = false;
    _armed_seen = false;
    _last_rx = _last_tx = now_us;
    _status.state = State::DISCOVERY;
    return true;
}

bool AP_SpektrumSmartESC_Motor::transmit(uint8_t length, uint32_t now_us, bool handshake)
{
    if (length == 0 || !_port.idle_high() || uint32_t(now_us - _last_rx) < 2000) {
        return false;
    }
    if (_port.collision() || !_port.begin_tx(_packet, length)) {
        fault(Fault::UART);
        return false;
    }
    _length = length;
    _tx = true;
    _handshake_tx = handshake;
    _tx_since = now_us;
    if (!handshake) { _last_control_start = now_us; }
    return true;
}

void AP_SpektrumSmartESC_Motor::update(const Demand &d, uint32_t now_us, uint32_t now_ms)
{
    if (_status.state == State::DISABLED || _status.state == State::FAULT) {
        return;
    }
    if (!owned(now_ms)) {
        fault(Fault::OWNERSHIP);
        return;
    }
    if (_port.collision()) {
        fault(Fault::UART);
        return;
    }
    if (_status.state == State::DISCOVERY && d.armed) {
        fault(Fault::DISARMED);
        return;
    }
    // Revoke authorization BEFORE servicing a pending transmission.
    if (_status.state != State::DISCOVERY) {
        if (!d.armed && _armed_seen) { fault(Fault::DISARMED); return; }
        if (!d.armed && (d.reverse || fabsf(d.magnitude) > 0)) { fault(Fault::INVALID_DEMAND); return; }
        if (d.armed && !_armed_seen) {
            _armed_seen = true;
            _initial_idle = false; // An arming edge must itself observe zero/forward/stopped.
        }
        if (!d.rc_ok && !d.armed && !_armed_seen) {
            // FC can boot before ELRS/RP4TD. Do not permanently fault an
            // otherwise healthy, DISARMED ESC session just because RC input
            // is not yet available. No 0xCD is transmitted here, and any
            // unexpected nonzero demand/reverse/failsafe still fails closed.
            if (d.magnitude != 0 || d.reverse || d.failsafe) {
                fault(Fault::INVALID_DEMAND);
            }
            return;
        }
        if (!d.rc_ok) { fault(Fault::RC_LOSS); return; }
        if (!d.control_ok || d.failsafe || uint32_t(now_ms - d.acquired_ms) >= 50) {
            fault(Fault::CONTROL_LOSS); return;
        }
        if ((d.reverse || _status.reverse || _transition) &&
            (!d.ground_authorized || uint32_t(now_ms - d.ground_ms) >= 100)) {
            // Only a reverse/transition request demands ground evidence.
            // Requiring it for every FORWARD tick would kill thrust in flight.
            fault(Fault::GROUND); return;
        }
        if (!isfinite(d.magnitude) || d.magnitude < 0 || d.magnitude > 1) {
            fault(Fault::INVALID_DEMAND); return;
        }
        if (d.reverse && (!_profile.reverse_verified || !_qualification.reverse_settings || !d.reverse_permitted)) {
            fault(Fault::REVERSE_DENIED); return;
        }
    }
    if (_tx) {
        if (!_port.transmission_complete()) {
            if (uint32_t(now_us - _tx_since) >= uint32_t(_length) * 87U + 1000U) {
                fault(Fault::TX_TIMEOUT);
            }
            return;
        }
        if (!_port.release_rx()) {
            fault(Fault::TURNAROUND);
            return;
        }
        _tx = false;
        uint32_t completed_us = now_us;
        uint32_t supplied_us;
        if (_port.completion_time_us(supplied_us)) {
            completed_us = supplied_us;
            if (uint32_t(now_us - completed_us) > 50000 || int32_t(completed_us - _tx_since) < -1000) {
                fault(Fault::TX_TIMEOUT);
                return;
            }
            if (int32_t(completed_us - _tx_since) < 0) {
                completed_us = _tx_since; // Conservative millisecond timestamp quantization.
            }
        }
        _status.tx_frames++;
        _status.tx_bytes += _length;
        _last_tx = _last_rx = completed_us;
        if (_handshake_tx) {
            if (!_reader.sent(completed_us)) {
                fault(Fault::PROTOCOL);
                return;
            }
        } else {
            _status.controls++;
            _status.throttle = _pending_throttle;
            _status.direction = _pending_direction;
            const bool next_reverse = _profile.reverse_verified && _pending_direction == _profile.reverse;
            if (next_reverse != _status.reverse) {
                _direction_changed = true;
                _direction_since = now_ms;
            }
            _status.reverse = next_reverse;
            _waiting_reply = true;
            _reply_since = completed_us;
        }
        return;
    }
    if (_status.state == State::DISCOVERY) {
        _reader.update(now_us);
        if (_reader.status().state == AP_SpektrumSmartESC_Reader::State::DISCOVERED) {
            _status.state = State::IDLE;
            _last_peer = now_us;
            _last_control_start = _last_tx;
            return;
        }
        if (!_reader.active()) {
            // No response from a not-yet-powered ESC is recoverable while
            // DISARMED. Unexpected/wrong-identity SRXL2 frames are NOT:
            // they remain protocol faults and must not auto-reconnect.
            fault(_reader.status().state == AP_SpektrumSmartESC_Reader::State::TIMEOUT ?
                Fault::ESC_LOSS : Fault::PROTOCOL);
            return;
        }
        if (!_port.idle_high() || uint32_t(now_us - _last_rx) < 2000) {
            return;
        }
        const uint8_t n = _reader.prepare(now_us, _packet, sizeof(_packet));
        transmit(n, now_us, true);
        return;
    }
    // Spektrum official protocol: after a VALID addressed 0x21 handshake,
    // normal running is CH1/CH7 in 0xCD and replyID-directed 0x80 sensor
    // data. 0x21 is NOT an every-50ms heartbeat. Our former 150ms
    // handshake watchdog shut down a healthy, replying ESC (P219 bench).
    // A source-less 0x80 never advances the authenticated handshake record.
    // For a dedicated, IO-owned SINGLE-ESC link it is valid as a *bounded*
    // response to our own actual completed 0xCD request, not as identity.
    constexpr uint32_t FIRST_SENSOR_GRACE_US = 100000U;
    constexpr uint32_t SENSOR_STALE_US = 75000U;
    if (!_profile.handshake_only_bench && _status.controls != 0 &&
        ((_have_telemetry && uint32_t(now_us - _telemetry_since) >= SENSOR_STALE_US) ||
         (!_have_telemetry && uint32_t(now_us - _last_peer) >= FIRST_SENSOR_GRACE_US))) {
        fault(Fault::ESC_LOSS);
        return;
    }
    if (_waiting_reply) {
        // Not every control frame gets an immediate 0x80. Allow an isolated
        // missing response, but never keep a stale link alive: the separate
        // freshness watchdog above is independent of this pending slot.
        if (uint32_t(now_us - _reply_since) >= 15000U) {
            _waiting_reply = false;
        } else {
            return;
        }
    }
    // Bench cadence, not a claim about Avian scheduling. Never catch up in bursts.
    if (uint32_t(now_us - _last_control_start) >= 11000) {
        control(d, now_us, now_ms);
    }
}

void AP_SpektrumSmartESC_Motor::control(const Demand &d, uint32_t now_us, uint32_t now_ms)
{
    const bool stopped = d.stopped && uint32_t(now_ms - d.stopped_ms) < 50;
    if (!_initial_idle) {
        if (d.magnitude > 0 || d.reverse) {
            return; // Reconnect or arming never resumes nonzero demand.
        }
        // Bootstrap ONLY with an explicit CH1=zero forward demand.
        // The ESC cannot report motor RPM until it has been asked for
        // telemetry by a 0xCD frame. Zero TX itself authorizes NO
        // nonzero throttle; initial_idle becomes true only after fresh
        // stopped evidence has been independently observed.
        if (stopped) {
            _initial_idle = true;
        }
    }
    float magnitude = d.magnitude;
    bool reverse = _status.reverse;
    if (_direction_changed && uint32_t(now_ms - _direction_since) < _profile.direction_dwell_ms) {
        magnitude = 0; // Hold zero after the direction-selection frame as well.
    }
    if (d.reverse != reverse) {
        _status.state = d.reverse ? State::REVERSE_REQUESTED : State::RETURN_TO_FORWARD;
        magnitude = 0;
        if (!_transition) {
            _transition = true;
            _neutral = false;
        } else {
            _status.state = State::THROTTLE_TO_IDLE;
        }
        if (stopped && _status.throttle == _profile.zero &&
            (!_direction_changed || uint32_t(now_ms - _direction_since) >= _profile.direction_dwell_ms)) {
            if (!_neutral) {
                _neutral = true;
                _neutral_since = now_ms;
            }
            _status.state = State::NEUTRAL_DWELL;
            if (uint32_t(now_ms - _neutral_since) >= _profile.direction_dwell_ms) {
                reverse = d.reverse;
                _transition = _neutral = false;
            }
        } else {
            _neutral = false;
        }
    } else {
        _transition = _neutral = false;
        _status.state = reverse ? State::REVERSE_ACTIVE : State::FORWARD;
    }
    uint16_t values[32] {};
    // Full verified ESC throttle range for the ordinary motor controller.
    // The optional 6% motor-turn test limits only its own requested demand.
    values[0] = uint16_t(_profile.zero + uint32_t(lroundf(magnitude * ((_profile.full - _profile.zero) / 4U))) * 4U);
    values[6] = reverse ? _profile.reverse : _profile.forward;
    const uint32_t mask = _profile.reverse_verified ? 0x41 : 1;
    const uint8_t n = AP_SpektrumSmartESC_ChannelData::encode(AP_SpektrumSmartESC_ChannelData::Kind::Normal,
        _target, 0, 0, mask, values, _packet, sizeof(_packet));
    _pending_throttle = values[0];
    _pending_direction = values[6];
    transmit(n, now_us, false);
}

void AP_SpektrumSmartESC_Motor::feed(uint8_t byte, uint32_t acquisition_us)
{
    if (_status.state == State::DISABLED || _status.state == State::FAULT) {
        return;
    }
    if (_tx) {
        fault(Fault::UART); // RX during owned TX is never evidence of an ESC reply.
        return;
    }
    _last_rx = acquisition_us;
    if (_status.state == State::DISCOVERY) {
        _reader.feed(byte, acquisition_us);
        return;
    }
    const auto before = _parser.observation().crc_frames;
    _parser.feed(byte, acquisition_us / 1000U);
    if (_parser.observation().crc_frames == before) {
        return;
    }
    uint8_t packet[80] {}, n = 0;
    if (!_parser.observed_frame(packet, sizeof(packet), n)) {
        fault(Fault::PROTOCOL);
        return;
    }
    // A real Avian sends unsolicited 0x21 every ~50ms after linking.
    // Do not classify its correctly addressed handshake as a protocol
    // failure, whether or not a 0x80 control reply is currently pending.
    if (n == 14 && packet[1] == 0x21 && packet[3] == _target &&
        packet[4] == _source) {
        _last_peer = acquisition_us;
        return;
    }
    if (!_waiting_reply ||
        uint32_t(acquisition_us - _reply_since) >= 15000U || n != 22 || packet[1] != 0x80 ||
        packet[3] != _source) {
        fault(Fault::PROTOCOL);
        return;
    }
    // Telemetry does not include a sender ID. Dedicated-bus ownership is required;
    // neither CRC nor a destination proves that the sender is an Avian 130A.
    // A 0x80 sensor packet has no source device ID. It may be a telemetry
    // candidate only while a separately authenticated 0x21 peer stays live.
    // An invalid/wrong sensor must not clear the reply deadline, refresh
    // the peer, or grant readiness to arm.
    AP_SpektrumSmartESC::Telemetry decoded {};
    if (!AP_SpektrumSmartESC::decode_esc(packet + 4, 16, decoded) ||
        decoded.valid == 0) {
        fault(Fault::PROTOCOL);
        return;
    }
    _waiting_reply = false;
    _telemetry = decoded;
    _have_telemetry = true;
    _telemetry_since = acquisition_us;
    _status.telemetry++;
}

bool AP_SpektrumSmartESC_Motor::telemetry(AP_SpektrumSmartESC::Telemetry &data, uint32_t now_us) const
{
    if (!_have_telemetry || _status.state == State::FAULT || _status.state == State::DISABLED ||
        uint32_t(now_us - _telemetry_since) >= 50000) {
        return false;
    }
    data = _telemetry;
    return true;
}
