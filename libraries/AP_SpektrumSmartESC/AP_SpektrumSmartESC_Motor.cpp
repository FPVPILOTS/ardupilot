/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Motor.h"
#include <math.h>

bool AP_SpektrumSmartESC_Motor::control_packet(const uint8_t *p, uint8_t n)
{
    if (p == nullptr || (n != 16 && n != 18) ||
        p[0] != 0xA6 || p[1] != 0xCD || p[2] != n ||
        p[3] != 0 || p[4] < 0x40 || p[4] > 0x4F ||
        p[5] != 0 || p[6] != 0 || p[7] != 0 ||
        p[9] != 0 || p[10] != 0 || p[11] != 0 ||
        (p[12] & 3U) != 0) {
        return false;
    }
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
    // Explicitly named, independently source-qualified experimental image:
    // CH1 only, no CH7 direction, and a bounded reference wire interval.
    // The FULL production whitelist is deliberately not available here.
    // These limits do not assert that the real ESC accepts these endpoints.
    const uint16_t throttle = uint16_t(p[12]) | (uint16_t(p[13]) << 8);
    return n == 16 && p[8] == 0x01 && throttle >= 768U &&
        throttle <= 4608U && AP_SpektrumSmartESC::crc16(p, n) == 0;
#else
    // Ordinary source-qualified production profile: CH1 and CH7 ONLY.
    return n == 18 && p[8] == 0x41 && (p[14] & 3U) == 0 &&
        AP_SpektrumSmartESC::crc16(p, n) == 0;
#endif
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
        // Preserve the ORIGINAL fault if hardware cleanup also fails.
        // Previously any UART/transport fault could be overwritten by
        // TURNAROUND here, obscuring the true pre-first-TC failure as F14.
        if (!released && _status.fault == Fault::NONE) {
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
    _bench_retry_at_us = 0;
    _uid = uid;
    _status.simulated = _port.simulated();
    _qualification = q;
    // Ordinary motor firmware requires independently qualified physical
    // capabilities. The EXPLICIT ArmedBench image uses a different contract:
    // a narrow source-level CH1-only packet surface and runtime proof of
    // IO ownership, real TC, zero RPM/throttle and healthy control/ESC link.
    // Do not set unverified qualification booleans to true to get there.
    const bool source_qualified = q.electrical && q.protocol && q.endpoints &&
        q.esc_loss_behavior && _profile.throttle_verified;
    const bool forward_only_bench = AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL &&
        q.io_firmware_crc == 0x6DBBAD71U && !_profile.reverse_verified &&
        !q.reverse_settings && !_profile.handshake_only_bench &&
        _profile.throttle_channel == 0 &&
        _profile.zero == 768U && _profile.full == 4608U;
    if ((!_port.simulated() && (!physical_control_available() ||
         (!_profile.reverse_verified && !forward_only_bench) ||
         (!q.reverse_settings && !forward_only_bench))) ||
        (!source_qualified && !forward_only_bench) ||
        _profile.throttle_channel != 0 ||
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

void AP_SpektrumSmartESC_Motor::update(const Demand &requested, uint32_t now_us, uint32_t now_ms)
{
    if (_status.state == State::DISABLED || _status.state == State::FAULT) {
        return;
    }
    // Only the isolated experimental ArmedBench image may acknowledge Plane
    // ARM while the ESC is still discovering. This is an observation-only
    // ARM edge: do not forward it to the motor until the pilot DISARMS and
    // explicitly ARMS again after genuine zero/stop telemetry is available.
    // In particular, a late-handshake link cannot launch the EDF on a stale
    // or raised throttle stick.
    Demand neutral {};
    const Demand *effective = &requested;
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
    if (requested.armed && !_armed_seen && !_status.bench_monitor &&
        !ready_to_arm(now_ms, now_us)) {
        _status.bench_monitor = true;
    }
    if (_status.bench_monitor) {
        if (!requested.armed) {
            _status.bench_monitor = false; // Real disarm/rearm required.
        } else {
            if (!requested.rc_ok || !requested.control_ok || requested.failsafe ||
                uint32_t(now_ms - requested.acquired_ms) >= 50U ||
                requested.reverse) {
                fault(requested.rc_ok ? Fault::CONTROL_LOSS : Fault::RC_LOSS);
                return;
            }
            neutral = requested;
            neutral.armed = false;
            neutral.magnitude = 0;
            neutral.reverse = false;
            effective = &neutral;
        }
    }
#endif
    const Demand &d = *effective;
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
        // FC/ESC may boot before ELRS/RP4TD has produced its first RC
        // sample. Keep the *dedicated SRXL2 slave* in RUNNING with CH1=zero
        // while DISARMED only; otherwise the ESC may time out and restart
        // handshake before a transmitter is switched on. Absolutely no
        // nonzero or reverse request can be sent through this exception.
        const bool waiting_for_rc_at_boot = !d.armed && !_armed_seen &&
            !d.rc_ok && d.magnitude == 0 && !d.reverse;
        if (!d.rc_ok && !waiting_for_rc_at_boot) {
            fault(Fault::RC_LOSS); return;
        }
        if ((!d.control_ok || d.failsafe) && !waiting_for_rc_at_boot) {
            fault(Fault::CONTROL_LOSS); return;
        }
        if (uint32_t(now_ms - d.acquired_ms) >= 50) {
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
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
            // ARM-before-link bench is monitor-only. Allow at most TWO
            // additional handshake windows with zero RX bytes, within the
            // same UART/IO lease and without any 0xCD control frames.
            // Wrong peers, CRC-invalid traffic, motor commands, UART
            // errors or nonzero/reverse demands never take this path.
            if (_status.bench_monitor && _status.controls == 0 &&
                _reader.status().state == AP_SpektrumSmartESC_Reader::State::TIMEOUT &&
                _reader.status().rx_bytes == 0) {
                if (_status.bench_link_retries >= 2) {
                    fault(Fault::ESC_LOSS);
                    return;
                }
                if (_bench_retry_at_us == 0) {
                    _bench_retry_at_us = now_us + 4000000U;
                    return;
                }
                if (int32_t(now_us - _bench_retry_at_us) < 0) {
                    return;
                }
                if (!_reader.start(true, true, _target, _uid, now_us, !_port.simulated())) {
                    fault(Fault::PROTOCOL);
                    return;
                }
                ++_status.bench_link_retries;
                _bench_retry_at_us = 0;
                return;
            }
#endif
            // Normal disarmed ESC-loss recovery remains in ChibiOS;
            // any invalid peer/protocol traffic fails closed.
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
    constexpr uint32_t MAX_COMPLETED_TX_SILENCE_US = 100000U;
    // TX progress and RX freshness are independent. Previously the
    // sensor/peer watchdog only ran AFTER the first completed control,
    // so a physically busy/low UART could prevent every 0xCD forever:
    // controls==0, no fresh reply required, and no automatic fault.
    // The timestamp advances ONLY after actual hardware TC, never when
    // a packet is queued or the bus happens to be idle.
    // This also bounds lost TX progress after a formerly healthy link.
    if (uint32_t(now_us - _last_tx) >= MAX_COMPLETED_TX_SILENCE_US) {
        fault(Fault::ESC_LOSS);
        return;
    }
    if (_profile.handshake_only_bench &&
        uint32_t(now_us - _last_peer) >= 100000U) {
        // Preserve the OLD finite, never-armable, no-sensor mock fallback.
        // This path is prohibited by the production profile.
        fault(Fault::ESC_LOSS);
        return;
    }
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
    if (n != 22 || packet[1] != 0x80 || packet[3] != _source) {
        fault(Fault::PROTOCOL);
        return;
    }
    // A CRC-correct ESC sensor response may be acquired after the 15ms
    // transaction window (or after the previous reply was already consumed).
    // It is NOT evidence of a fresh command, but dropping that one late
    // response need not declare the entire link corrupt. The independent
    // sensor/TC watchdog still faults if valid timely replies stop.
    const bool reply_current = _waiting_reply &&
        int32_t(acquisition_us - _reply_since) >= 0 &&
        uint32_t(acquisition_us - _reply_since) < 15000U;
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
    if (!reply_current) {
        _status.late_sensors++;
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
