/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC.h"
#include <string.h>
#if defined(SPKTSMART_NATIVE_CRC_TEST) || (!defined(SPKTSMART_STANDALONE_TEST) && !defined(SPKTSMART_UART4_REGISTER_TEST) && !defined(SPKTSMART_RX_REGISTER_TEST))
#include <AP_Math/crc.h>
#endif

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t(p[0]) << 8) | p[1];
}

AP_SpektrumSmartESC::AP_SpektrumSmartESC(uint8_t local_id, uint8_t esc_id, uint32_t timeout_ms, bool passive_bus) :
    _local_id(local_id), _esc_id(esc_id), _timeout_ms(timeout_ms), _passive_bus(passive_bus)
{
}

uint16_t AP_SpektrumSmartESC::crc16(const uint8_t *data, size_t length)
{
#if defined(SPKTSMART_NATIVE_CRC_TEST) || (!defined(SPKTSMART_STANDALONE_TEST) && !defined(SPKTSMART_UART4_REGISTER_TEST) && !defined(SPKTSMART_RX_REGISTER_TEST))
    // Same implementation as AP_RCProtocol's SRXL_CRC_CALCULATE. No srxlInit*
    // calls or callbacks: the normal RC receiver owns that global library.
    return crc16_ccitt(data, length, 0);
#else
    // Independent small host-test oracle; never part of vehicle firmware.
    uint16_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= uint16_t(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000U) ? (crc << 1) ^ 0x1021U : crc << 1;
        }
    }
    return crc;
#endif
}

uint8_t AP_SpektrumSmartESC::encode(uint8_t type, const uint8_t *payload, uint8_t payload_length,
                                  uint8_t *packet, size_t capacity)
{
    if (packet == nullptr || (payload == nullptr && payload_length != 0) ||
        payload_length > MAX_PACKET - 5 || capacity < size_t(payload_length) + 5) {
        return 0;
    }
    // memmove permits the payload to be inside the destination buffer.
    if (payload_length != 0) {
        memmove(packet + 3, payload, payload_length);
    }
    const uint8_t length = payload_length + 5;
    packet[0] = 0xA6;
    packet[1] = type;
    packet[2] = length;
    const uint16_t crc = crc16(packet, length - 2);
    packet[length - 2] = crc >> 8;
    packet[length - 1] = crc;
    return length;
}

bool AP_SpektrumSmartESC::decode_channels(const uint8_t *packet, size_t length, Channels &result)
{
    if (packet == nullptr || length < 14 || length > MAX_PACKET || packet[0] != 0xA6 ||
        packet[1] != 0xCD || packet[2] != length || packet[3] > 1 || crc16(packet, length) != 0) {
        return false;
    }
    Channels next {};
    next.failsafe = packet[3] == 1;
    next.reply_id = packet[4];
    next.rssi = int8_t(packet[5]);
    next.frame_losses = uint16_t(packet[6]) | (uint16_t(packet[7]) << 8);
    next.mask = uint32_t(packet[8]) | (uint32_t(packet[9]) << 8) |
                (uint32_t(packet[10]) << 16) | (uint32_t(packet[11]) << 24);
    size_t offset = 12;
    for (uint8_t channel = 0; channel < 32; channel++) {
        if ((next.mask & (uint32_t(1) << channel)) == 0) {
            continue;
        }
        if (offset + 2 > length - 2) {
            return false;
        }
        next.values[channel] = uint16_t(packet[offset]) | (uint16_t(packet[offset + 1]) << 8);
        offset += 2;
    }
    if (offset != length - 2) {
        return false;
    }
    result = next;
    return true;
}

void AP_SpektrumSmartESC::update(uint32_t now_ms)
{
    if (_passive_bus && _master_id != 0 && uint32_t(now_ms - _master_ms) >= _timeout_ms) {
        _master_id = 0;
        _have_poll = false;
        _have_telemetry = false;
    }
    if (_used != 0 && uint32_t(now_ms - _last_byte_ms) >= FRAME_TIMEOUT_MS) {
        _used = 0;
        _counters.incomplete++;
    }
    if (_have_device && uint32_t(now_ms - _device_ms) >= _timeout_ms) {
        _have_device = false;
        _have_telemetry = false;
    }
    if (_have_telemetry && uint32_t(now_ms - _telemetry_ms) >= _timeout_ms) {
        _have_telemetry = false;
    }
}

void AP_SpektrumSmartESC::reset_observation()
{
    _used = 0;
    _have_device = false;
    _have_telemetry = false;
    _master_id = 0;
    _have_poll = false;
}

bool AP_SpektrumSmartESC::feed(uint8_t byte, uint32_t now_ms)
{
    update(now_ms);
    if (_used == 0 && byte != 0xA6) {
        return false;
    }
    _last_byte_ms = now_ms;
    _buffer[_used++] = byte;
    if (_used == 3 && (_buffer[2] < 5 || _buffer[2] > MAX_PACKET)) {
        _counters.length_errors++;
        _used = 0;
        return false;
    }
    if (_used < 3 || _used != _buffer[2]) {
        return false;
    }
    _used = 0;
    if (crc16(_buffer, _buffer[2]) != 0) {
        _counters.crc_errors++;
        return false;
    }
    _observation.crc_frames++;
    _observation.last_ms = now_ms;
    _observation.type = _buffer[1];
    _observation.sensor = (_buffer[1] == 0x80 && _buffer[2] == 22) ? _buffer[4] : 0;
    if (_buffer[1] == 0x21 && _buffer[2] == 14) {
        _observation.handshake_source = _buffer[3];
    }
    const bool accepted = parse(now_ms);
    if (accepted) {
        _observation.accepted++;
    }
    return accepted;
}

bool AP_SpektrumSmartESC::observed_frame(uint8_t *packet, size_t capacity, uint8_t &length) const
{
    length = 0;
    const uint8_t n = _buffer[2];
    if (_used != 0 || _observation.crc_frames == 0 || packet == nullptr || n < 5 ||
        n > MAX_PACKET || capacity < n || crc16(_buffer, n) != 0) {
        return false;
    }
    memcpy(packet, _buffer, n);
    length = n;
    return true;
}

bool AP_SpektrumSmartESC::parse(uint32_t now_ms)
{
    const uint8_t length = _buffer[2];
    if (_buffer[1] == 0x21) {
        if (length != 14) {
            _counters.length_errors++;
            return false;
        }
        // A passive tap learns the telemetry destination from the observed bus
        // master's final broadcast, not from a guessed receiver address.
        if (_passive_bus && _buffer[3] >= 0x10 && _buffer[3] < 0x40 && _buffer[4] == 0xFF) {
            // This diagnostic transport is fixed at 115200. An agreed higher
            // rate is not a capability advertisement and cannot be ignored.
            if (_buffer[6] != 0) {
                reset_observation();
                _counters.unsupported++;
                return false;
            }
            _master_id = _buffer[3];
            _master_ms = now_ms;
            _have_poll = false;
            _have_telemetry = false;
            return true;
        }
        if ((_esc_id >> 4) != 4 || (_local_id >> 4) != 3 || _timeout_ms == 0 ||
            _buffer[3] != _esc_id ||
            _buffer[5] < 1 || _buffer[5] > 100 || _buffer[6] > 1 ||
            (_passive_bus && _buffer[4] == 0xFF) ||
            (_buffer[4] != _local_id && _buffer[4] != 0 && _buffer[4] != 0xFF &&
             !(_passive_bus && _buffer[4] >= 0x10 && _buffer[4] < 0x40))) {
            _counters.rejected++;
            return false;
        }
        Device next {_buffer[3], _buffer[5], _buffer[6], _buffer[7],
                     uint32_t(_buffer[8]) | (uint32_t(_buffer[9]) << 8) |
                     (uint32_t(_buffer[10]) << 16) | (uint32_t(_buffer[11]) << 24)};
        _device = next;
        _device_ms = now_ms;
        _have_device = true;
        _have_telemetry = false; // A handshake starts a new observation session.
        _counters.handshakes++;
        return true;
    }
    if (_passive_bus && _buffer[1] == 0xCD) {
        Channels channels {};
        _have_poll = false;
        if (!decode_channels(_buffer, length, channels) || _master_id == 0) {
            _counters.rejected++;
            return false;
        }
        _master_ms = now_ms;
        if (channels.failsafe) {
            _have_telemetry = false;
        } else if (_have_device && channels.reply_id == _esc_id) {
            _have_poll = true;
            _poll_ms = now_ms;
            _counters.polls++;
        }
        return true;
    }
    if (_buffer[1] == 0x80) {
        if (length != 22) {
            _counters.length_errors++;
            return false;
        }
        // Telemetry has a destination, not a source ID. Only appropriate for an
        // isolated single-ESC experiment; this does not authenticate the sender.
        const uint8_t destination = _passive_bus ? _master_id : _local_id;
        if (!_have_device || destination == 0 || _buffer[3] != destination ||
            (_passive_bus && (!_have_poll || uint32_t(now_ms - _poll_ms) >= 5))) {
            _counters.rejected++;
            return false;
        }
        _have_poll = false; // At most one telemetry response per observed poll.
        Telemetry next {};
        if (!decode_esc(&_buffer[4], 16, next)) {
            _counters.unsupported++;
            return false;
        }
        _telemetry = next;
        _telemetry_ms = now_ms;
        _device_ms = now_ms;
        _have_telemetry = true;
        _counters.telemetry++;
        return true;
    }
    _counters.unsupported++;
    return false;
}

bool AP_SpektrumSmartESC::device(Device &result, uint32_t now_ms) const
{
    if (!_have_device || uint32_t(now_ms - _device_ms) >= _timeout_ms) {
        return false;
    }
    result = _device;
    return true;
}

bool AP_SpektrumSmartESC::telemetry(Telemetry &result, uint32_t now_ms) const
{
    if (!_have_device || !_have_telemetry || uint32_t(now_ms - _device_ms) >= _timeout_ms ||
        uint32_t(now_ms - _telemetry_ms) >= _timeout_ms) {
        return false;
    }
    result = _telemetry;
    return true;
}

bool AP_SpektrumSmartESC::decode_esc(const uint8_t *data, size_t length, Telemetry &result)
{
    // STRU_TELE_ESC in AP_RCTelemetry/spektrumTelemetrySensors.h, big endian.
    if (data == nullptr || length != 16 || data[0] != 0x20) {
        return false;
    }
    Telemetry next {};
    next.secondary_id = data[1];
    const uint16_t rpm = be16(data + 2);
    if (rpm != 0xFFFF) {
        next.valid |= ERPM;
        next.electrical_rpm = uint32_t(rpm) * 10;
    }
    float *fields[] = {&next.voltage, &next.temperature_c, &next.current, &next.bec_temperature_c};
    const float scale[] = {0.01f, 0.1f, 0.01f, 0.1f};
    for (uint8_t i = 0; i < 4; i++) {
        const uint16_t value = be16(data + 4 + 2 * i);
        if (value != 0xFFFF) {
            next.valid |= 1U << (i + 1);
            *fields[i] = value * scale[i];
        }
    }
    float *small_fields[] = {&next.bec_current, &next.bec_voltage, &next.throttle_pct, &next.power_pct};
    const float small_scale[] = {0.1f, 0.05f, 0.5f, 0.5f};
    for (uint8_t i = 0; i < 4; i++) {
        if (data[12 + i] != 0xFF && (i != 2 || data[14] <= 200)) {
            next.valid |= 1U << (i + 5);
            *small_fields[i] = data[12 + i] * small_scale[i];
        }
    }
    result = next;
    return true;
}
