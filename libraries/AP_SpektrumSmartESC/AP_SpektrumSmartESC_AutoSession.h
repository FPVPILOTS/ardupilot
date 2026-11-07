/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "AP_SpektrumSmartESC_AutoConnect.h"
#include "AP_SpektrumSmartESC_Reader.h"

// Coordinates the Phase 17 startup policy with the existing, CRC-validating
// SRXL2 handshake reader. This is a *transport-neutral* integration:
// - begin() requests a bounded handshake, but never sends a UART byte;
// - caller must own an independently qualified half-duplex UART/physical TX gate;
// - 0xCD Control Data / throttle / reverse are deliberately not exposed;
// - LINKED is not a motor-arming decision, or evidence of accepted telemetry.
// The current HSLab one-attempt lease is NOT made reusable or bypassed.
class AP_SpektrumSmartESC_AutoSession {
public:
    using State = AP_SpektrumSmartESC_AutoConnect::State;
    using Action = AP_SpektrumSmartESC_AutoConnect::Action;

    void enable(uint32_t now_ms, uint8_t target, uint32_t uid)
    {
        _reader.stop();
        _target = target;
        _uid = uid;
        _policy.enable(now_ms, target);
    }

    void disable()
    {
        _reader.stop();
        _policy.disable();
    }

    // Call only under a separately reviewed transport safety gate. The
    // controller must abort its native lease whenever this returns no active
    // handshake and its previously-owned UART transaction is still pending.
    Action tick(uint32_t now_us, bool disarmed_bench_safe,
                bool bus_idle_high, bool uart_rx_restored)
    {
        const uint32_t now_ms = now_us / 1000U;
        if (_policy.status().state == State::HANDSHAKE) {
            _reader.update(now_us);
            const auto &s = _reader.status();
            if (s.state == AP_SpektrumSmartESC_Reader::State::DISCOVERED) {
                // A final broadcast queued in software is not enough:
                // Reader enters DISCOVERED only after sent() following UART TC.
                if (uart_rx_restored) {
                    (void)_policy.handshake_complete(now_ms, s.device,
                        s.responses == 1, uart_rx_restored);
                }
            } else if (s.state == AP_SpektrumSmartESC_Reader::State::TIMEOUT ||
                       s.state == AP_SpektrumSmartESC_Reader::State::FAULT ||
                       s.state == AP_SpektrumSmartESC_Reader::State::BLOCKED) {
                _policy.handshake_failed(now_ms);
            }
        }
        const auto action = _policy.update(now_ms, disarmed_bench_safe, bus_idle_high);
        if (_policy.status().state != State::HANDSHAKE &&
            _reader.active()) {
            _reader.stop(); // clear stale prepared packet / callbacks
        }
        if (action == Action::BEGIN_HANDSHAKE) {
            // HSLab POWERON_SOURCE 0x30, 2 s PREPARED and 5 s fixed window.
            // This does not relax the native one-shot authorization.
            if (!_reader.start(true, disarmed_bench_safe, _target, _uid,
                               now_us, true)) {
                _policy.handshake_failed(now_ms);
                return Action::NONE;
            }
        }
        return action;
    }

    uint8_t prepare(uint32_t now_us, uint8_t *packet, size_t capacity, bool idle_high)
    {
        if (_policy.status().state != State::HANDSHAKE) {
            return 0;
        }
        return _reader.prepare(now_us, packet, capacity, idle_high);
    }

    bool take_startup_announcement(uint8_t *out, size_t capacity)
    {
        return _policy.status().state == State::HANDSHAKE &&
            _reader.take_startup_announcement(out, capacity);
    }

    bool sent(uint32_t tc_time_us)
    {
        return _policy.status().state == State::HANDSHAKE && _reader.sent(tc_time_us);
    }

    void feed(uint8_t byte, uint32_t acquisition_us)
    {
        if (_policy.status().state == State::HANDSHAKE) {
            _reader.feed(byte, acquisition_us);
        }
    }

    // Called with a fully captured incoming packet from the already-owned
    // isolated bus. A CRC-valid 0x21 *peer advertisement* maintains link
    // freshness, never authorizes motor readiness or a telemetry sample.
    bool observe_peer(const uint8_t *packet, uint8_t length, uint32_t now_ms)
    {
        if (_policy.status().state != State::LINKED || packet == nullptr ||
            length != 14 || packet[0] != 0xA6 || packet[1] != 0x21 ||
            packet[2] != length || packet[3] != _target ||
            (packet[4] != 0 && packet[4] != 0x30 && packet[4] != 0xFF) ||
            packet[5] == 0 || packet[5] > 100 ||
            packet[6] > 1 || AP_SpektrumSmartESC::crc16(packet, length) != 0) {
            return false;
        }
        return _policy.peer_seen(now_ms, _target, true);
    }

    void competing_master()
    {
        _reader.stop();
        _policy.competing_master();
    }

    void uart_fault()
    {
        _reader.stop();
        _policy.uart_fault();
    }

    const AP_SpektrumSmartESC_AutoConnect::Status &status() const
    {
        return _policy.status();
    }

    const AP_SpektrumSmartESC_Reader::Status &reader_status() const
    {
        return _reader.status();
    }

private:
    AP_SpektrumSmartESC_AutoConnect _policy;
    AP_SpektrumSmartESC_Reader _reader;
    uint8_t _target = 0;
    uint32_t _uid = 0;
};
