/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

// Passive SRXL2 observation only. No transport, throttle or reverse output API.
// Observing an ESC handshake is not proof that an Avian is safe to control.
class AP_SpektrumSmartESC {
public:
    static constexpr uint8_t MAX_PACKET = 80;
    static constexpr uint32_t FRAME_TIMEOUT_MS = 5;
    struct Device {
        uint8_t id;
        uint8_t priority;
        uint8_t baud_mask;
        uint8_t info;
        uint32_t uid;
    };
    enum Field : uint16_t {
        ERPM = 1U << 0,
        VOLTAGE = 1U << 1,
        TEMPERATURE = 1U << 2,
        CURRENT = 1U << 3,
        BEC_TEMPERATURE = 1U << 4,
        BEC_CURRENT = 1U << 5,
        BEC_VOLTAGE = 1U << 6,
        THROTTLE = 1U << 7,
        POWER = 1U << 8,
    };
    struct Telemetry {
        uint16_t valid;
        uint32_t electrical_rpm; // Not mechanical RPM: requires motor pole count.
        float voltage;
        float temperature_c;
        float current;
        float bec_temperature_c;
        float bec_current;
        float bec_voltage;
        float throttle_pct;
        float power_pct;
        uint8_t secondary_id;
    };
    struct Counters {
        uint32_t crc_errors;
        uint32_t length_errors;
        uint32_t incomplete;
        uint32_t rejected;
        uint32_t unsupported;
        uint32_t handshakes;
        uint32_t telemetry;
        uint32_t polls;
    };
    struct Observation {
        uint32_t crc_frames = 0; // CRC-valid framing, NOT accepted/authenticated ESC data.
        // CRC-valid raw packet types. These do not prove ownership, a live peer,
        // authorization, motor readiness or accepted ESC telemetry.
        uint32_t handshake_frames = 0; // 0x21
        uint32_t control_frames = 0;   // 0xCD; observed only, never sent here
        uint32_t sensor_frames = 0;    // 0x80; may be untrusted/unsolicited
        uint32_t other_frames = 0;
        uint32_t accepted = 0;
        uint32_t last_ms = 0;
        uint8_t type = 0;
        uint8_t sensor = 0; // 0 unless last frame is a complete telemetry envelope.
        uint8_t handshake_source = 0; // Last observed, not necessarily expected ESC ID.
    };
    struct Channels {
        bool failsafe;
        uint8_t reply_id;
        int8_t rssi;
        uint16_t frame_losses;
        uint32_t mask;
        uint16_t values[32]; // Full-range SRXL2 units, not PWM microseconds.
    };

    AP_SpektrumSmartESC(uint8_t local_id = 0x30, uint8_t esc_id = 0x40,
                       uint32_t timeout_ms = 50, bool passive_bus = false);
    bool feed(uint8_t byte, uint32_t now_ms);
    void update(uint32_t now_ms);
    void reset_observation();
    bool device(Device &device, uint32_t now_ms) const;
    bool telemetry(Telemetry &data, uint32_t now_ms) const;
    // Copy immediately when observation().crc_frames advances, before feeding another byte.
    bool observed_frame(uint8_t *packet, size_t capacity, uint8_t &length) const;
    const Observation &observation() const { return _observation; }
    const Counters &counters() const { return _counters; }
    static uint16_t crc16(const uint8_t *data, size_t length);
    static bool decode_esc(const uint8_t *data, size_t length, Telemetry &result);
    static bool decode_channels(const uint8_t *packet, size_t length, Channels &result);
    // Serialize a frame into caller-owned memory for simulation. Does not send.
    static uint8_t encode(uint8_t type, const uint8_t *payload, uint8_t payload_length,
                          uint8_t *packet, size_t capacity);

private:
    bool parse(uint32_t now_ms);
    uint8_t _local_id;
    uint8_t _esc_id;
    uint32_t _timeout_ms;
    bool _passive_bus;
    uint8_t _master_id = 0;
    uint32_t _master_ms = 0;
    bool _have_poll = false;
    uint32_t _poll_ms = 0;
    uint8_t _buffer[MAX_PACKET] {};
    uint8_t _used = 0;
    uint32_t _last_byte_ms = 0;
    uint32_t _device_ms = 0;
    uint32_t _telemetry_ms = 0;
    bool _have_device = false;
    bool _have_telemetry = false;
    Device _device {};
    Telemetry _telemetry {};
    Counters _counters {};
    Observation _observation {};
};
