/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// Spektrum SRXL2 source (SpektrumRC/SRXL2/Source/spm_srxl.c):
// 0x21 -> dest=0 indicates a cold restart/brownout;
// 0x80 -> dest=0 means no RF forwarding endpoint;
// 0x80 -> dest=0xFF requests the bus master to repeat handshake.
// This classifier is invoked ONLY for complete, CRC-valid frames after a
// correctly addressed discovery and on an isolated single-ESC bus.
// It does not establish peer identity, grant ARM, or interpret throttle.
class AP_SpektrumSmartESC_RunningPolicy {
public:
    enum class Action : uint8_t {
        REJECT, PEER_ALIVE, RESTART, SENSOR, SENSOR_RESTART, IGNORE_OPTIONAL
    };
    static Action classify(const uint8_t *p, uint8_t len, uint8_t fc, uint8_t esc)
    {
        if (p == nullptr || len < 5 || p[0] != 0xA6 || p[2] != len) {
            return Action::REJECT;
        }
        if (p[1] == 0x21) {
            // Complete addressed startup; another controller or non-ESC is
            // never authorized by the previously authenticated peer.
            if (len != 14 || p[3] != esc || p[5] == 0 || p[5] > 100 ||
                p[6] > 1) {
                return Action::REJECT;
            }
            if (p[4] == 0x00) {
                return Action::RESTART;
            }
            return p[4] == fc ? Action::PEER_ALIVE : Action::REJECT;
        }
        if (p[1] == 0x80) {
            if (len != 22) {
                return Action::REJECT;
            }
            if (p[3] == 0xFF) {
                return Action::SENSOR_RESTART;
            }
            return p[3] == 0x00 || p[3] == fc ?
                Action::SENSOR : Action::REJECT;
        }
        // Official parser recognizes these packet types. They carry no
        // throttle authority and must never refresh a verified ESC link.
        // Ignore only when framing and CRC have already been checked.
        if (p[1] == 0x41 || p[1] == 0x50 || p[1] == 0x55) {
            return Action::IGNORE_OPTIONAL;
        }
        // In particular, a control packet from another purported master
        // while WE own the isolated ESC bus is not harmless.
        return Action::REJECT;
    }
};
