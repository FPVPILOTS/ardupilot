/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Reader.h"

// Experimental permission, deliberately independent of electrical qualification.
// Neither policy has a retry/reset API: one experiment per constructed instance.
class AP_SpektrumSmartESC_LabHandshake {
public:
    class Authorization {
    public:
        bool acknowledge(int8_t value, bool safe, uint32_t now_ms)
        {
            _pending = value == 15 && safe && !_used;
            _ack_ms = now_ms;
            return _pending;
        }
        bool ready(uint32_t now_ms) const { return _pending && !_used && uint32_t(now_ms - _ack_ms) < 10000; }
        bool consume(bool safe, uint32_t now_ms)
        {
            const bool allowed = safe && ready(now_ms);
            _pending = false;
            _used |= allowed;
            return allowed;
        }
        void cancel() { _pending = false; }
        bool used() const { return _used; }
    private:
        bool _pending = false, _used = false;
        uint32_t _ack_ms = 0;
    };

    // Native UART owns this second bound, independent of main-task scheduling.
    class Lease {
    public:
        bool start(uint8_t target, uint32_t uid, uint32_t now_ms, bool power_on = false)
        {
            if (_used || target < 0x40 || target > 0x4F) {
                return false;
            }
            _active = _used = true;
            _started_ms = now_ms;
            _target = target;
            _uid = uid;
            _power_on = power_on;
            return true;
        }
        uint32_t duration_ms() const { return _power_on ? AP_SpektrumSmartESC_Reader::PREPARE_MS + AP_SpektrumSmartESC_Reader::WINDOW_MS : 200; }
        bool active(uint32_t now_ms) const { return _active && uint32_t(now_ms - _started_ms) < duration_ms(); }
        bool take(const uint8_t *packet, uint8_t length, uint32_t now_ms)
        {
            if (!active(now_ms) || uint32_t(now_ms - _started_ms) >= duration_ms() - 3 || _confirmed ||
                !AP_SpektrumSmartESC_Reader::handshake_packet(packet, length) ||
                packet[3] != (_power_on ? AP_SpektrumSmartESC_Reader::POWERON_SOURCE : AP_SpektrumSmartESC_Reader::SOURCE)) {
                return false;
            }
            if (_power_on) {
                if (uint32_t(now_ms - _started_ms) < AP_SpektrumSmartESC_Reader::PREPARE_MS ||
                    (packet[4] == _target && (_frames >= AP_SpektrumSmartESC_Reader::MAX_QUERIES ||
                     (_frames != 0 && uint32_t(now_ms - _last_query_ms) < AP_SpektrumSmartESC_Reader::RETRY_MS))) ||
                    (packet[4] == 0xFF && _frames == 0) || (packet[4] != _target && packet[4] != 0xFF)) {
                    return false;
                }
            } else if (_frames >= 2 || packet[4] != (_frames == 0 ? _target : 0xFF)) {
                return false;
            }
            const uint32_t uid = uint32_t(packet[8]) | (uint32_t(packet[9]) << 8) |
                (uint32_t(packet[10]) << 16) | (uint32_t(packet[11]) << 24);
            if (uid != _uid) {
                return false;
            }
            _frames++;
            _confirmed = packet[4] == 0xFF;
            if (!_confirmed) {
                _last_query_ms = now_ms;
            }
            return true;
        }
        void close() { _active = false; }
        // Read-only observation for the separate AutoLab native lease manager.
        // Regular HSLab remains one-shot; this does not reset authorization.
        bool used() const { return _used; }
        uint8_t frames() const { return _frames; }
    private:
        uint32_t _started_ms = 0, _uid = 0, _last_query_ms = 0;
        uint8_t _target = 0, _frames = 0;
        bool _active = false, _used = false, _power_on = false, _confirmed = false;
    };
};
