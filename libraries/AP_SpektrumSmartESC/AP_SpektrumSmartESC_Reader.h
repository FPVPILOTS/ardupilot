/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Bounded dedicated-bus FC-master experiment, not automatic master election.
// Framing/CRC reuse the existing parser. No Control Data or polling API exists.
class AP_SpektrumSmartESC_Reader {
public:
    static constexpr uint8_t SOURCE = 0x31; // FC unit 1, NOT proof of master authority.
    static constexpr uint8_t POWERON_SOURCE = 0x30; // WingFlight's FC-unit-0 profile, not receiver emulation.
#ifndef AP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT
#define AP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT 0
#endif
#if AP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT
    // Dedicated AutoLinkFast and *explicit opt-in* ArmedBench only:
    // 50ms receive-first period before requesting ESC 0x40. This is
    // relative to the driver start, NOT the Pixhawk MCU reset or the
    // ESC battery edge. Other boards retain the original 2-second window.
    // UART idle, half-duplex, TC, CRC and RX restoration remain mandatory.
    static constexpr uint32_t PREPARE_MS = 50, WINDOW_MS = 5000, RETRY_MS = 50;
#else
    static constexpr uint32_t PREPARE_MS = 2000, WINDOW_MS = 5000, RETRY_MS = 50;
#endif
    static constexpr uint8_t MAX_QUERIES = WINDOW_MS / RETRY_MS;
    enum class State : uint8_t { DISABLED, WAITING, QUERY_TX, RESPONSE, CONFIRM_TX,
                                 DISCOVERED, TIMEOUT, FAULT, BLOCKED, PREPARED };
    struct Status {
        State state = State::DISABLED;
        uint32_t rx_bytes = 0, rx_frames = 0, crc_errors = 0;
        uint32_t tx_frames = 0, tx_bytes = 0, echoes = 0, timeouts = 0, unexpected = 0;
        uint32_t stale_bytes = 0;
        // Timings relative to this single reader attempt in milliseconds.
        // Diagnostic ONLY: none of these fields establishes a link or
        // permits a TX. A destination-zero announcement is not a reply.
        uint32_t first_announcement_ms = 0, last_announcement_ms = 0;
        uint32_t first_targeted_tc_ms = 0, last_targeted_tc_ms = 0;
        uint32_t last_targeted_reply_ms = 0;
        // CRC-valid startup announcements are observed, never accepted as a
        // solicited response or proof of LINKED state.
        uint32_t startup_announcements = 0;
        uint8_t last_hs_src = 0, last_hs_dst = 0;
        uint8_t last_hs_priority = 0, last_hs_baud = 0;
        uint8_t responses = 0; // Requested, CRC-valid peer replies; excludes advertisements/echoes.
        uint32_t uid = 0, negotiated_baud = 0;
        uint8_t device = 0, priority = 0, baud_mask = 0, info = 0, type = 0;
        uint8_t source = SOURCE, queued = 0;
    };
    bool start(bool authorized, bool disarmed, uint8_t target, uint32_t uid, uint32_t now_us, bool power_on = false);
    void update(uint32_t now_us);
    uint8_t prepare(uint32_t now_us, uint8_t *packet, size_t capacity, bool bus_idle = true);
    bool sent(uint32_t now_us); // Only after actual UART TC, never DMA-empty/queued.
    void feed(uint8_t byte, uint32_t acquisition_us);
    void fault() { _status.state = State::FAULT; }
    void stop() { _status.state = State::DISABLED; }
    bool active() const;
    const Status &status() const { return _status; }
    // Bounded raw prefix from this attempt, including malformed input and echoes.
    // Diagnostic caller supplies only bytes acquired by the physical UART.
    uint8_t capture(uint8_t *out, size_t capacity) const;
    // Returns one CRC-validated unsolicited startup handshake received during
    // the bounded power-on window; never treats it as a solicited response.
    // The native UART gate independently validates it before early TX.
    bool take_startup_announcement(uint8_t *out, size_t capacity, uint32_t &acquired_ms);
    static bool handshake_packet(const uint8_t *p, size_t n);
private:
    void observe(const uint8_t *p, uint8_t n, uint32_t now_us);
    AP_SpektrumSmartESC _parser;
    Status _status;
    uint32_t _started = 0, _phase = 0, _uid = 0, _last_activity = 0;
    uint32_t _next_query = 0;
    uint8_t _target = 0;
    bool _prepared = false, _power_on = false;
    uint8_t _capture[64] {};
    uint8_t _captured = 0;
    uint8_t _startup_packet[14] {};
    bool _startup_pending = false;
    uint32_t _startup_acquired_ms = 0;
};
