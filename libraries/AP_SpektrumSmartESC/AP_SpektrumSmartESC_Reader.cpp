/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Reader.h"
#include <string.h>

constexpr uint8_t AP_SpektrumSmartESC_Reader::SOURCE;

bool AP_SpektrumSmartESC_Reader::handshake_packet(const uint8_t *p, size_t n)
{
    return p != nullptr && n == 14 && p[0] == 0xA6 && p[1] == 0x21 && p[2] == 14 &&
        ((p[3] == SOURCE && p[5] == 20) || (p[3] == POWERON_SOURCE && p[5] == 10)) &&
        ((p[4] >= 0x40 && p[4] <= 0x4F) || p[4] == 0xFF) &&
        p[6] == 0 && p[7] == 0 && AP_SpektrumSmartESC::crc16(p, n) == 0;
}

bool AP_SpektrumSmartESC_Reader::start(bool authorized, bool disarmed, uint8_t target,
                                     uint32_t uid, uint32_t now_us, bool power_on)
{
    _status = {};
    _prepared = false;
    _captured = 0;
    _startup_pending = false;
    _startup_acquired_ms = 0;
    _power_on = power_on;
    _status.source = power_on ? POWERON_SOURCE : SOURCE;
    if (!authorized || !disarmed || target < 0x40 || target > 0x4F) {
        _status.state = State::BLOCKED;
        return false;
    }
    _parser = AP_SpektrumSmartESC(_status.source, target, 50, false);
    _uid = uid; _target = target; _started = _phase = _last_activity = now_us;
    _next_query = now_us + (power_on ? PREPARE_MS * 1000U : 50000U);
    _status.state = power_on ? State::PREPARED : State::WAITING;
    return true;
}

bool AP_SpektrumSmartESC_Reader::active() const
{
    return (_status.state >= State::WAITING && _status.state <= State::CONFIRM_TX) || _status.state == State::PREPARED;
}

void AP_SpektrumSmartESC_Reader::update(uint32_t now_us)
{
    const uint32_t deadline = _power_on ? (PREPARE_MS + WINDOW_MS) * 1000U : 200000U;
    if (active() && uint32_t(now_us - _started) >= deadline) {
        _status.timeouts++;
        _status.state = State::TIMEOUT; // No retries or restart loop.
    } else if (_status.state == State::PREPARED && uint32_t(now_us - _started) >= PREPARE_MS * 1000U) {
        _status.state = State::WAITING;
    } else if (_status.state == State::RESPONSE && uint32_t(now_us - _phase) >= 50000) {
        if (_power_on && _status.queued < MAX_QUERIES) {
            _status.state = State::WAITING; // Only the explicitly authorized, finite power-on window.
        } else {
            _status.timeouts++;
            _status.state = State::TIMEOUT;
        }
    }
}

uint8_t AP_SpektrumSmartESC_Reader::prepare(uint32_t now_us, uint8_t *packet, size_t capacity, bool bus_idle)
{
    update(now_us);
    if (_prepared || !active() || !bus_idle || _status.state == State::PREPARED) {
        return 0;
    }
    const uint32_t deadline = _power_on ? (PREPARE_MS + WINDOW_MS) * 1000U : 200000U;
    if (uint32_t(now_us - _started) >= deadline - 3000U) {
        _status.timeouts++;
        _status.state = State::TIMEOUT; // Reserve the native 3-ms watchdog window.
        return 0;
    }
    // RX timestamps are millisecond acquisition samples. Two milliseconds
    // conservatively exceeds the required two 115200-baud character times.
    if (_status.rx_bytes != 0 && uint32_t(now_us - _last_activity) < 2000) {
        return 0;
    }
    if (_status.state == State::WAITING) {
        if (int32_t(now_us - _next_query) < 0) {
            return 0;
        }
        _status.state = State::QUERY_TX;
    }
    if (_status.state != State::QUERY_TX && _status.state != State::CONFIRM_TX) {
        return 0;
    }
    const uint8_t payload[] = {_status.source, uint8_t(_status.state == State::QUERY_TX ? _target : 0xFF),
        uint8_t(_power_on ? 10 : 20), 0, 0, uint8_t(_uid), uint8_t(_uid >> 8), uint8_t(_uid >> 16), uint8_t(_uid >> 24)};
    const uint8_t length = AP_SpektrumSmartESC::encode(0x21, payload, sizeof(payload), packet, capacity);
    _prepared = length != 0;
    if (_prepared) {
        _status.queued++;
    }
    return length;
}

bool AP_SpektrumSmartESC_Reader::sent(uint32_t now_us)
{
    update(now_us);
    if (!_prepared || (_status.state != State::QUERY_TX && _status.state != State::CONFIRM_TX)) {
        return false;
    }
    _status.tx_frames++; _status.tx_bytes += 14;
    _prepared = false;
    _phase = now_us;
    if (_status.state == State::QUERY_TX) {
        const uint32_t offset_ms = uint32_t(now_us - _started) / 1000U;
        if (_status.tx_frames == 1) {
            _status.first_targeted_tc_ms = offset_ms;
        }
        _status.last_targeted_tc_ms = offset_ms;
        _status.state = State::RESPONSE;
        // Fixed lower bound and small deterministic jitter prevent phase locking
        // with the slave's 50-ms announcements. No catch-up bursts after stalls.
        _next_query = now_us + (RETRY_MS + ((_uid + _status.tx_frames * 3U) % 8U)) * 1000U;
    } else {
        _status.negotiated_baud = 115200; // Final broadcast selects 115200 even if peer supports 400k.
        _status.state = State::DISCOVERED;
    }
    return true;
}

void AP_SpektrumSmartESC_Reader::feed(uint8_t byte, uint32_t acquisition_us)
{
    if (!active()) {
        return;
    }
    // A manually started attempt can inherit older bytes from the RX FIFO/ring.
    // Do not let their unsigned age look like a 200-ms deadline expiration or
    // accept an old reply as evidence for this attempt. Handles clock wrap.
    if (int32_t(acquisition_us - _started) < 0) {
        _status.stale_bytes++;
        return;
    }
    update(acquisition_us);
    if (!active()) {
        return;
    }
    _status.rx_bytes++;
    if (_captured < sizeof(_capture)) {
        _capture[_captured++] = byte;
    }
    _last_activity = acquisition_us;
    const uint32_t prior = _parser.observation().crc_frames;
    _parser.feed(byte, uint32_t(acquisition_us - _started) / 1000U);
    _status.crc_errors = _parser.counters().crc_errors;
    if (_parser.observation().crc_frames != prior) {
        uint8_t packet[AP_SpektrumSmartESC::MAX_PACKET], length;
        _status.rx_frames++;
        if (_parser.observed_frame(packet, sizeof(packet), length)) {
            observe(packet, length, acquisition_us);
        }
    }
}

bool AP_SpektrumSmartESC_Reader::take_startup_announcement(uint8_t *out, size_t capacity, uint32_t &acquired_ms)
{
    if (!_startup_pending || out == nullptr || capacity < sizeof(_startup_packet)) {
        return false;
    }
    memcpy(out, _startup_packet, sizeof(_startup_packet));
    acquired_ms = _startup_acquired_ms;
    _startup_pending = false;
    return true;
}

uint8_t AP_SpektrumSmartESC_Reader::capture(uint8_t *out, size_t capacity) const
{
    if (out == nullptr) {
        return 0;
    }
    const uint8_t length = capacity < _captured ? capacity : _captured;
    memcpy(out, _capture, length);
    return length;
}

void AP_SpektrumSmartESC_Reader::observe(const uint8_t *p, uint8_t n, uint32_t now_us)
{
    _status.type = p[1];
    if (n == 14 && p[1] == 0x21) {
        // Protocol evidence only. Never promote unsolicited 0x40->0x00
        // announcements to a targeted response, control, or motor readiness.
        _status.last_hs_src = p[3];
        _status.last_hs_dst = p[4];
        _status.last_hs_priority = p[5];
        _status.last_hs_baud = p[6];
    }
    if (n == 14 && p[1] == 0x21 && p[3] == _status.source) {
        const uint32_t uid = uint32_t(p[8]) | (uint32_t(p[9]) << 8) |
                             (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
        if (uid == _uid) {
            _status.echoes++; // Never counts as a discovered device or a peer response.
            return;
        }
    }
    if (n == 14 && p[1] == 0x21 && p[3] == _target && p[5] >= 1 && p[5] <= 100 && p[6] <= 1) {
        if (p[4] == 0 && (_status.state == State::WAITING || (_power_on &&
            (_status.state == State::PREPARED || _status.state == State::RESPONSE)))) {
            const uint32_t offset_ms = uint32_t(now_us - _started) / 1000U;
            if (_status.startup_announcements == 0) {
                _status.first_announcement_ms = offset_ms;
            }
            _status.last_announcement_ms = offset_ms;
            _status.startup_announcements++;
            // Only an addressed, CRC-validated, spontaneous ESC announce in
            // PREPARED may request an early targeted handshake. No CRC-invalid
            // data, echoes, unsolicited control, or other device may trigger it.
            if (_power_on && _status.state == State::PREPARED &&
                _status.tx_frames == 0 && !_startup_pending &&
                p[3] == _target && p[5] > 0 && p[5] <= 100 &&
                p[6] <= 1 && AP_SpektrumSmartESC::crc16(p, n) == 0) {
                memcpy(_startup_packet, p, sizeof(_startup_packet));
                _startup_pending = true;
                _startup_acquired_ms = now_us / 1000U;
                // Two full milliseconds of idle after the final received byte
                // exceeds the SRXL2 two-character half-duplex idle requirement.
                _next_query = now_us + 2000U;
                _status.state = State::WAITING;
            }
            return; // Unprompted advertisement is evidence, not the requested response.
        }
        if (p[4] == _status.source && _status.state == State::RESPONSE && uint32_t(now_us - _phase) < 50000) {
            _status.responses++;
            _status.last_targeted_reply_ms = uint32_t(now_us - _started) / 1000U;
            _status.device = p[3]; _status.priority = p[5]; _status.baud_mask = p[6]; _status.info = p[7];
            _status.uid = uint32_t(p[8]) | (uint32_t(p[9]) << 8) |
                          (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
            _status.state = State::CONFIRM_TX;
            return;
        }
    }
    _status.unexpected++;
    fault(); // Other master, unexpected ID/destination or control/telemetry: release to RX.
}
