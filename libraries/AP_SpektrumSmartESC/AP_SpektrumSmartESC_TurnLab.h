/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_ChannelData.h"
#include "AP_SpektrumSmartESC_Commands.h"
#include "AP_SpektrumSmartESC_PollLab.h"

// Fixed forward-only experiment. These durations are TEST limits, not claimed
// Avian neutral/direction/stop requirements. Never used by Motor's 0..1 path.
class AP_SpektrumSmartESC_TurnLab {
public:
    static constexpr uint32_t PERIOD_MS = 20;
    static constexpr uint32_t PHASE_MS = 500;
    static constexpr uint32_t WINDOW_MS = 1500;
    static constexpr uint32_t ZERO_WINDOW_MS = 3000; // 3 seconds of CH1 minimum, no motor demand
    static constexpr uint32_t STALE_MS = 60;
    // Only for the one-shot, unarmable CH1=minimum experiment. A source-less
    // 0x80 is NOT peer identity and cannot authorize armed/motor mode.
    static constexpr uint32_t ZERO_REPLY_WINDOW_MS = 12;
    static constexpr uint32_t ZERO_REPLY_FRESH_MS = 50;
    static constexpr uint32_t ZERO_AUTHENTIC_PEER_MAX_MS = ZERO_WINDOW_MS + 100;
    static constexpr uint8_t LENGTH = 18;
    static constexpr uint8_t ZERO_LENGTH = 16; // CH1 only; no CH7 direction change
    // This is a scheduler hint matching the *existing* native half-duplex
    // guard. It cannot authorize a packet, claim UART ownership, or bypass
    // the native last-RX recheck. Expected ESC heartbeats can end in the
    // same millisecond as the scheduled 20ms control slot.
    static bool tx_window(uint32_t now, uint32_t last_uart_rx_ms,
                          bool rx_restored, bool rx_quiet)
    {
        return rx_restored && rx_quiet &&
            AP_SpektrumSmartESC_PollLab::rx_gap_safe(now, last_uart_rx_ms);
    }
    enum class State : uint8_t { IDLE, NEUTRAL, FORWARD6, FINAL_NEUTRAL, DONE, FAULT, BLOCKED };
    struct Profile {
        AP_SpektrumSmartESC_Commands::Profile channels;
        uint32_t io_crc = 0;
        bool electrical = false;
        bool loss_behavior = false;
        // TurnLab commands CH7 = forward only. Validated forward polarity is
        // required, but a reverse endpoint belongs to the separate full motor
        // qualification and must not block this forward-only experiment.
        bool forward_verified = false;
        bool valid() const
        {
            return electrical && loss_behavior && io_crc != 0 &&
                channels.throttle_verified && forward_verified &&
                channels.throttle_channel == 0 && channels.reverse_channel == 6 &&
                value(channels.zero) && value(channels.full) && channels.zero < channels.full &&
                value(channels.forward);
        }
        // Reference-only CH1 idle trial. NOT authorization for physical motion.
        // Keep full TurnLab qualification independent of this protocol experiment.
        bool zero_only_candidate() const
        {
            return io_crc != 0 && channels.throttle_channel == 0 &&
                channels.zero == 768U && channels.full == 64768U;
        }
        static bool value(uint16_t v) { return v <= 65532U && (v & 3U) == 0; }
    };
    struct Evidence {
        bool disarmed = false, emergency_stop = false, owner = false;
        bool peer = false, uart_ok = false;
        uint32_t peer_ms = 0, control_ms = 0, owner_ms = 0, reply_ms = 0;
        // Peer_ms is ONLY a verified 0x21 from the addressed ESC. Never
        // overwrite it with the source-less 0x80 receipt. During the
        // unarmable 3-second CH1-minimum lease, allow the specifically
        // qualified recent reply witness to replace *periodic 0x21 timing*,
        // not initial handshake/identity. Motor-turn still requires 100ms
        // 0x21 and ignores the new reply_ms field.
        bool safe(uint32_t now, bool zero_only = false) const
        {
            const uint32_t peer_age = uint32_t(now - peer_ms);
            const bool authentic_peer_fresh = peer_ms != 0 &&
                peer_age < (zero_only ? 200U : 100U);
            const bool bounded_zero_reply = zero_only && peer_ms != 0 &&
                reply_ms != 0 && int32_t(reply_ms - peer_ms) >= 0 &&
                uint32_t(now - reply_ms) < ZERO_REPLY_FRESH_MS &&
                peer_age < ZERO_AUTHENTIC_PEER_MAX_MS;
            return disarmed && emergency_stop && owner && peer && uart_ok &&
                (authentic_peer_fresh || bounded_zero_reply) &&
                uint32_t(now - control_ms) < STALE_MS &&
                uint32_t(now - owner_ms) < 50;
        }
    };

    explicit AP_SpektrumSmartESC_TurnLab(const Profile &p) : _profile(p) {}
    // Both the coordinator and native UART enforce this bounded contract.
    // A failed request consumes this object permanently, including bad profile.
    bool start(bool authorized, const Evidence &e, uint32_t now, bool zero_only = false)
    {
        if (_used) { return false; }
        _used = true;
        _zero_only = zero_only;
        if (!authorized || !(zero_only ? _profile.zero_only_candidate() : _profile.valid()) || !e.safe(now, zero_only)) {
            _state = State::BLOCKED;
            return false;
        }
        _start = _last = now;
        _state = State::NEUTRAL;
        return true;
    }
    bool active(uint32_t now) const
    {
        return running() && uint32_t(now - _start) < (_zero_only ? ZERO_WINDOW_MS : WINDOW_MS);
    }
    bool running() const
    {
        return _state == State::NEUTRAL || _state == State::FORWARD6 || _state == State::FINAL_NEUTRAL;
    }
    void abort() { if (running()) { _state = State::FAULT; } }
    State state() const { return _state; }
    bool used() const { return _used; }
    uint32_t frames() const { return _frames; }
    uint32_t completed() const { return _completed; }
    uint16_t throttle() const { return _throttle; }
    uint16_t six_percent() const
    {
        // Round to the nearest wire quantum (bits 15:2), exactly 6/100.
        const uint32_t steps = (_profile.channels.full - _profile.channels.zero) / 4U;
        return _profile.channels.zero + ((steps * 6U + 50U) / 100U) * 4U;
    }
    bool zero_only() const { return _zero_only; }
    uint8_t encode(State phase, uint8_t *packet, size_t capacity) const
    {
        if (_zero_only) {
            if (!_profile.zero_only_candidate() || phase != State::NEUTRAL) { return 0; }
            // CH1 ONLY. No CH7, no movement command above candidate minimum.
            uint16_t values[32] {};
            values[0] = _profile.channels.zero;
            return AP_SpektrumSmartESC_ChannelData::encode(AP_SpektrumSmartESC_ChannelData::Kind::Normal,
                0x40, 0, 0, 0x01, values, packet, capacity);
        }
        if (!_profile.valid() || (phase != State::NEUTRAL && phase != State::FORWARD6 && phase != State::FINAL_NEUTRAL)) {
            return 0;
        }
        uint16_t channels[32] {};
        channels[0] = phase == State::FORWARD6 ? six_percent() : _profile.channels.zero;
        channels[6] = _profile.channels.forward; // Never send reverse in TurnLab.
        return AP_SpektrumSmartESC_ChannelData::encode(AP_SpektrumSmartESC_ChannelData::Kind::Normal,
            0x40, 0, 0, 0x41, channels, packet, capacity);
    }
    // Called at the 50-Hz vehicle boundary. No fabricated sensor timestamps:
    // control_ms is this script's current supervision tick, peer_ms physical RX.
    uint8_t prepare(const Evidence &e, uint32_t now, uint8_t *packet, size_t capacity)
    {
        if (!running()) { return 0; }
        if (!e.safe(now, _zero_only) || uint32_t(now - _last) >= STALE_MS) { abort(); return 0; }
        const uint32_t elapsed = now - _start;
        if (elapsed >= (_zero_only ? ZERO_WINDOW_MS : WINDOW_MS)) {
            _state = !_pending && (_zero_only ? _neutral_completed : _final_completed) ? State::DONE : State::FAULT;
            return 0;
        }
        if (_pending) { return 0; }
        if (_frames != 0 && uint32_t(now - _last) < PERIOD_MS) { return 0; }
        const State phase = _zero_only || elapsed < PHASE_MS ? State::NEUTRAL :
                            elapsed < 2U * PHASE_MS ? State::FORWARD6 : State::FINAL_NEUTRAL;
        if ((phase == State::FORWARD6 && !_neutral_completed) ||
            (phase == State::FINAL_NEUTRAL && !_pulse_completed)) { abort(); return 0; }
        return encode(phase, packet, capacity);
    }
    // Exact native whitelist: packet CRC, mask, target, channel order, forward
    // polarity, phase timing, per-frame budget and previous true TC all checked.
    bool take(const uint8_t *packet, uint8_t length, const Evidence &e, uint32_t now)
    {
        uint8_t expected[LENGTH] {};
        const uint8_t required = _zero_only ? ZERO_LENGTH : LENGTH;
        if (packet == nullptr || length != required || prepare(e, now, expected, sizeof(expected)) != required) { return false; }
        for (uint8_t i = 0; i < required; i++) { if (packet[i] != expected[i]) { return false; } }
        const uint32_t elapsed = now - _start;
        _state = _zero_only || elapsed < PHASE_MS ? State::NEUTRAL :
                 elapsed < 2U * PHASE_MS ? State::FORWARD6 : State::FINAL_NEUTRAL;
        _throttle = uint16_t(packet[12]) | (uint16_t(packet[13]) << 8);
        _last = now;
        _frames++;
        _pending = true;
        return true;
    }
    bool complete(uint32_t now, bool rx_restored)
    {
        if (!running() || !_pending || !rx_restored || uint32_t(now - _last) >= STALE_MS) { abort(); return false; }
        _pending = false;
        _completed++;
        _neutral_completed |= _state == State::NEUTRAL;
        _pulse_completed |= _state == State::FORWARD6;
        _final_completed |= _state == State::FINAL_NEUTRAL;
        return true; // TC proves local serialization, NOT an ESC-applied command.
    }
private:
    Profile _profile;
    State _state = State::IDLE;
    uint32_t _start = 0, _last = 0, _frames = 0, _completed = 0;
    uint16_t _throttle = 0;
    bool _used = false, _pending = false, _zero_only = false;
    bool _neutral_completed = false, _pulse_completed = false, _final_completed = false;
};
