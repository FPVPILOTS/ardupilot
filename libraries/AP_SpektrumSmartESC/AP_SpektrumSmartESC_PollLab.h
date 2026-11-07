/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "AP_SpektrumSmartESC.h"

// Physical TX is only ever compiled into the dedicated, unarmable PollLab
// hwdef. This packet is a SRXL2 CONTROL message, NOT a motor-stop command.
// Never infer physical motor safety from a zero channel update mask.
// A completely disconnected motor and bench electrical review are REQUIRED.
class AP_SpektrumSmartESC_PollLab {
public:
    static constexpr uint8_t TARGET = 0x40;
    static constexpr uint8_t DESTINATION = 0x30;
    static constexpr uint32_t LEASE_MS = 100;

    static uint8_t encode(uint8_t *out, size_t capacity)
    {
        // Command=0; ReplyID=0x40; RSSI=0; frame losses=0;
        // update mask=0; no throttle, reverse, or other channel entries.
        constexpr uint8_t payload[] = {0x00, TARGET, 0, 0, 0, 0, 0, 0, 0};
        return AP_SpektrumSmartESC::encode(0xCD, payload, sizeof(payload), out, capacity);
    }

    static bool exact_packet(const uint8_t *p, uint8_t n)
    {
        if (p == nullptr || n != 14 || p[0] != 0xA6 || p[1] != 0xCD ||
            p[2] != n || AP_SpektrumSmartESC::crc16(p, n) != 0) {
            return false;
        }
        const uint8_t payload[] = {0, TARGET, 0, 0, 0, 0, 0, 0, 0};
        for (uint8_t i = 0; i < sizeof(payload); i++) {
            if (p[3 + i] != payload[i]) {
                return false;
            }
        }
        return true;
    }

    // Bounded *native* one-packet lease; no caller may reset or extend it.
    // Must be started only with fresh operator approval and physical isolation.
    class Lease {
    public:
        bool start(uint32_t now_ms)
        {
            if (_used) {
                return false;
            }
            _used = _active = true;
            _started_ms = now_ms;
            return true;
        }

        bool active(uint32_t now_ms) const
        {
            return _active && uint32_t(now_ms - _started_ms) < LEASE_MS;
        }

        bool take(const uint8_t *packet, uint8_t length, uint32_t now_ms)
        {
            if (!active(now_ms) || _frame_used || !exact_packet(packet, length)) {
                return false;
            }
            _frame_used = true; // One TX attempt maximum; never retry.
            return true;
        }

        bool used() const { return _used; }
        void close() { _active = false; }

    private:
        uint32_t _started_ms = 0;
        bool _used = false;
        bool _active = false;
        bool _frame_used = false;
    };
};
