/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Phase 17E-C: dry-run-only 0xCD ReplyID readiness checker. The preview is
// constructed in RAM and is NEVER sent to an ESC. No UART, HAL, motor, or
// transmit API exists in this class. In SRXL2, mask==0 means "no channel
// updates", NOT throttle zero, disarm, idle, or confirmed motor safety.
class AP_SpektrumSmartESC_PollDryRun {
public:
    enum class Reason : uint8_t {
        NO_LINK,
        BENCH_UNSAFE,
        WRONG_PEER,
        COMPETING_MASTER,
        UART_CHANGED,
        RX_NOT_READY,
        PEER_STALE,
        SETTLING,
        BUS_NOT_IDLE,
        PREVIEW_INVALID,
        READY,
        ALREADY_PREVIEWED,
    };
    struct Evidence {
        uint32_t now_ms = 0;
        uint32_t last_peer_ms = 0;
        uint32_t uart_errors = 0;
        uint8_t peer_id = 0;
        bool linked = false;
        bool disarmed = false;
        bool emergency_stop = false;
        bool rx_restored = false;
        bool bus_idle = false;
        bool competing_master = false;
    };
    struct Status {
        Reason reason = Reason::NO_LINK;
        uint32_t previews = 0;
        uint32_t link_sessions = 0;
        uint32_t blocked = 0;
        uint8_t last_preview_length = 0;
    };

    // Return true exactly once after a stable LINKED session has passed the
    // read-only readiness check. A true result NEVER permits physical TX.
    bool update(const Evidence &e)
    {
        if (!e.linked) {
            _active = false;
            _previewed = false;
            _fault_latched = false;
            _status.reason = Reason::NO_LINK;
            return false;
        }
        if (!_active) {
            _active = true;
            _previewed = false;
            _fault_latched = false;
            _link_ms = e.now_ms;
            _error_baseline = e.uart_errors;
            _status.link_sessions++;
            _status.last_preview_length = 0;
        }
        // Latch defects for the current connection: do not permit a
        // transiently clear frame to erase a prior unsafe observation.
        if (e.uart_errors != _error_baseline) {
            _fault_latched = true;
        }
        if (_fault_latched) {
            return block(Reason::UART_CHANGED);
        }
        if (!e.disarmed || !e.emergency_stop) {
            return block(Reason::BENCH_UNSAFE);
        }
        if (e.competing_master) {
            _fault_latched = true;
            return block(Reason::COMPETING_MASTER);
        }
        if (e.peer_id != 0x40) {
            return block(Reason::WRONG_PEER);
        }
        if (!e.rx_restored) {
            return block(Reason::RX_NOT_READY);
        }
        if (int32_t(e.now_ms - e.last_peer_ms) < 0 ||
            uint32_t(e.now_ms - e.last_peer_ms) > 100U) {
            return block(Reason::PEER_STALE);
        }
        if (uint32_t(e.now_ms - _link_ms) < 1000U) {
            return block(Reason::SETTLING);
        }
        if (!e.bus_idle) {
            return block(Reason::BUS_NOT_IDLE);
        }
        if (_previewed) {
            _status.reason = Reason::ALREADY_PREVIEWED;
            return false;
        }
        // Command=0 (Channel Data), ReplyID=0x40, RSSI and losses=0,
        // mask=0, NO channel values. NOT a motor-safe stop command.
        const uint8_t payload[9] = {0, 0x40, 0, 0, 0, 0, 0, 0, 0};
        uint8_t output[14] {};
        const uint8_t length = AP_SpektrumSmartESC::encode(
            0xCD, payload, sizeof(payload), output, sizeof(output));
        AP_SpektrumSmartESC::Channels decoded {};
        if (length != 14 || AP_SpektrumSmartESC::crc16(output, length) != 0 ||
            !AP_SpektrumSmartESC::decode_channels(output, length, decoded) ||
            decoded.failsafe || decoded.reply_id != 0x40 || decoded.mask != 0) {
            _fault_latched = true;
            return block(Reason::PREVIEW_INVALID);
        }
        _previewed = true;
        _status.reason = Reason::READY;
        _status.previews++;
        _status.last_preview_length = length;
        return true;
    }

    const Status &status() const { return _status; }
    static const char *reason_name(Reason reason)
    {
        switch (reason) {
        case Reason::NO_LINK: return "NO_LINK";
        case Reason::BENCH_UNSAFE: return "BENCH_UNSAFE";
        case Reason::WRONG_PEER: return "WRONG_PEER";
        case Reason::COMPETING_MASTER: return "MASTER_CONFLICT";
        case Reason::UART_CHANGED: return "UART_CHANGED";
        case Reason::RX_NOT_READY: return "RX_NOT_READY";
        case Reason::PEER_STALE: return "PEER_STALE";
        case Reason::SETTLING: return "SETTLING";
        case Reason::BUS_NOT_IDLE: return "BUS_NOT_IDLE";
        case Reason::PREVIEW_INVALID: return "INVALID";
        case Reason::READY: return "READY_PREVIEW";
        case Reason::ALREADY_PREVIEWED: return "PREVIEWED";
        }
        return "UNKNOWN";
    }

private:
    bool block(Reason reason)
    {
        _status.reason = reason;
        _status.blocked++;
        return false;
    }

    Status _status {};
    uint32_t _link_ms = 0, _error_baseline = 0;
    bool _active = false, _previewed = false, _fault_latched = false;
};
