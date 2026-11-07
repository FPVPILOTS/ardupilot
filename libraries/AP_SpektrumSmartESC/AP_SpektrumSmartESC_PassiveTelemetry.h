/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "AP_SpektrumSmartESC.h"

// Phase 17E-A: OBSERVATION ONLY on an already-established SRXL2 bus.
//
// 0x80 is an inbound telemetry sensor packet containing a 16-byte Spektrum
// sensor payload. 0xCD is a control packet with a reply ID and potentially
// motor channel demands. This class deliberately has NO UART, transmit,
// command, poll, arming or motor interface.
//
// Even CRC-valid telemetry 0x80 has a DESTINATION but no SOURCE DEVICE ID.
// A sample observed on the isolated bus is an UNAUTHENTICATED CANDIDATE,
// not independently verified ESC telemetry. Never automatically publish it
// into AP_ESC_Telem or use it for throttle/arming/failsafe decisions.
class AP_SpektrumSmartESC_PassiveTelemetry {
public:
    static constexpr uint32_t CANDIDATE_TIMEOUT_MS = 100;

    struct Status {
        uint32_t handshake_frames = 0; // 0x21, CRC-valid
        uint32_t control_frames = 0;   // 0xCD, CRC-valid; never transmitted
        uint32_t sensor_frames = 0;    // 0x80, CRC-valid; not all are ESC data
        uint32_t other_frames = 0;
        uint32_t esc_candidates = 0;   // correct-length, addressed ESC sensor
        uint32_t rejected_sensors = 0; // wrong length, destination or fields
        uint32_t other_sensors = 0;    // sensor ID != 0x20
        uint8_t last_sensor_id = 0, last_secondary_id = 0;
        uint8_t last_destination = 0;
        uint32_t last_candidate_ms = 0;
    };

    void observe_crc_frame(const uint8_t *packet, uint8_t length,
                           uint32_t acquired_ms, uint8_t destination)
    {
        // Defend even if caller mistakenly passes malformed or stale bytes.
        if (packet == nullptr || length < 5 ||
            length > AP_SpektrumSmartESC::MAX_PACKET ||
            packet[0] != 0xA6 || packet[2] != length ||
            AP_SpektrumSmartESC::crc16(packet, length) != 0) {
            return;
        }
        switch (packet[1]) {
        case 0x21:
            _status.handshake_frames++;
            return;
        case 0xCD:
            _status.control_frames++;
            return;
        case 0x80:
            _status.sensor_frames++;
            break;
        default:
            _status.other_frames++;
            return;
        }

        // A new sensor frame immediately revokes any older displayed value,
        // even when malformed or of an unknown type.
        invalidate();
        _status.last_destination = length > 3 ? packet[3] : 0;
        _status.last_sensor_id = length > 4 ? packet[4] : 0;
        _status.last_secondary_id = length > 5 ? packet[5] : 0;
        if (length != 22 || packet[3] != destination) {
            _status.rejected_sensors++;
            return;
        }
        if (packet[4] != 0x20) {
            _status.other_sensors++;
            return;
        }
        AP_SpektrumSmartESC::Telemetry decoded {};
        if (!AP_SpektrumSmartESC::decode_esc(packet + 4, 16, decoded) ||
            decoded.valid == 0) {
            _status.rejected_sensors++;
            return;
        }
        _candidate = decoded;
        _status.esc_candidates++;
        _status.last_candidate_ms = acquired_ms;
        _have_candidate = true;
    }

    bool candidate(AP_SpektrumSmartESC::Telemetry &out, uint32_t now_ms) const
    {
        if (!_have_candidate ||
            uint32_t(now_ms - _status.last_candidate_ms) >= CANDIDATE_TIMEOUT_MS) {
            return false;
        }
        out = _candidate;
        return true;
    }

    // Called on link loss, prearm loss or a new handshake; never carry old
    // candidate values across a power-cycle or reconnect.
    void invalidate()
    {
        _have_candidate = false;
        _candidate = {};
    }

    const Status &status() const { return _status; }

private:
    Status _status {};
    AP_SpektrumSmartESC::Telemetry _candidate {};
    bool _have_candidate = false;
};
