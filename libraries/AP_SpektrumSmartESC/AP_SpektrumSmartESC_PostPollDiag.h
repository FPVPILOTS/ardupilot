/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"
#include <stdint.h>

// Read-side witness for an already-authorized PollLab 0xCD transaction.
// No UART ownership, no TX, no packet acknowledgement, no ESC telemetry output.
// Timestamps are the existing 1-ms FIFO acquisition times (not oscilloscope time).
class AP_SpektrumSmartESC_PostPollDiag {
public:
    struct Snapshot {
        uint32_t bytes = 0;
        uint32_t pre_tc = 0;
        uint32_t bins[4] {}; // 0..2, 3..5, 6..12, 13..80 ms after true TC
        uint32_t syncs = 0;
        uint32_t good_crc = 0;
        uint32_t bad_crc = 0;
        uint32_t bad_length = 0;
        uint32_t incomplete = 0;
        uint32_t handshake = 0;
        uint32_t control = 0;
        uint32_t sensors = 0;
        uint32_t late_sensors = 0; // Complete 0x80 beyond 12ms, observation only
        uint32_t other = 0;
        uint8_t prefix[8] {};
        uint8_t prefix_len = 0;
        bool tc = false;
    };

    void begin()
    {
        _snapshot = Snapshot{};
        _used = 0;
        _active = true;
    }

    void on_true_tc(uint32_t timestamp_ms)
    {
        if (_active && !_snapshot.tc) {
            _tc_ms = timestamp_ms;
            _snapshot.tc = true;
        }
    }

    void feed(uint8_t byte, uint32_t acquired_ms)
    {
        if (!_active) {
            return;
        }
        if (!_snapshot.tc || int32_t(acquired_ms - _tc_ms) < 0) {
            _snapshot.pre_tc++;
            return;
        }
        const uint32_t elapsed = acquired_ms - _tc_ms;
        if (elapsed > 80U) {
            return;
        }
        _snapshot.bytes++;
        _snapshot.bins[elapsed <= 2 ? 0 : (elapsed <= 5 ? 1 : (elapsed <= 12 ? 2 : 3))]++;
        if (_snapshot.prefix_len < sizeof(_snapshot.prefix)) {
            _snapshot.prefix[_snapshot.prefix_len++] = byte;
        }

        if (_used != 0 && uint32_t(acquired_ms - _last_byte_ms) >= 5U) {
            _snapshot.incomplete++;
            _used = 0;
        }
        _last_byte_ms = acquired_ms;
        if (_used == 0 && byte != 0xA6) {
            return;
        }
        if (_used == 0) {
            _snapshot.syncs++;
        }
        _packet[_used++] = byte;
        if (_used == 3 && (_packet[2] < 5 || _packet[2] > sizeof(_packet))) {
            _snapshot.bad_length++;
            _used = 0;
            return;
        }
        if (_used < 3 || _used != _packet[2]) {
            return;
        }
        const uint8_t length = _used;
        _used = 0;
        if (AP_SpektrumSmartESC::crc16(_packet, length) != 0) {
            _snapshot.bad_crc++;
            return;
        }
        _snapshot.good_crc++;
        switch (_packet[1]) {
        case 0x21:
            _snapshot.handshake++;
            break;
        case 0xCD:
            _snapshot.control++;
            break;
        case 0x80:
            if (length == 22) {
                _snapshot.sensors++;
                if (elapsed > 12U) {
                    _snapshot.late_sensors++;
                }
            } else {
                _snapshot.bad_length++;
            }
            break;
        default:
            _snapshot.other++;
            break;
        }
    }

    void finish()
    {
        if (_active && _used != 0) {
            _snapshot.incomplete++;
        }
        _used = 0;
        _active = false;
    }

    bool active() const { return _active; }
    const Snapshot &snapshot() const { return _snapshot; }

private:
    Snapshot _snapshot {};
    uint8_t _packet[AP_SpektrumSmartESC::MAX_PACKET] {};
    uint8_t _used = 0;
    uint32_t _last_byte_ms = 0;
    uint32_t _tc_ms = 0;
    bool _active = false;
};
