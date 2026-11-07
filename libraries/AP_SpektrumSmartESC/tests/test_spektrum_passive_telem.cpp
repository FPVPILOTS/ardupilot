/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_PassiveTelemetry.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using Inspector = AP_SpektrumSmartESC_PassiveTelemetry;
using Parser = AP_SpektrumSmartESC;

static uint8_t frame(uint8_t kind, const uint8_t *payload, uint8_t size,
                     uint8_t *out, size_t capacity)
{
    return Parser::encode(kind, payload, size, out, capacity);
}

static uint8_t esc_frame(uint8_t *out, uint8_t destination = 0x30,
                         uint8_t sensor = 0x20, bool all_missing = false)
{
    // Spektrum 0x80 uses 1 destination byte + 16 sensor bytes.
    // ESC sensor 0x20: electrical RPM, voltage, temperature, current, etc.
    uint8_t p[] = {
        destination, sensor, 0x00,
        0x00, 0x64, // 1000 eRPM (raw 100, *10)
        0x09, 0xC4, // 25.00 V
        0x01, 0x2C, // 30.0 C
        0x00, 0xFA, // 2.50 A
        0x00, 0xC8, // 20.0 C BEC
        0x0A,       // 1.0 A BEC
        0x64,       // 5.00 V BEC
        0xFF,       // throttle unknown
        0xFF        // power unknown
    };
    static_assert(sizeof(p) == 17, "1 destination + 16 Spektrum sensor bytes");
    if (all_missing) {
        memset(p + 3, 0xFF, sizeof(p) - 3);
    }
    return frame(0x80, p, sizeof(p), out, 80);
}

static void classify_without_tx()
{
    Inspector inspector;
    uint8_t hand[] = {0x40, 0x30, 10, 0, 0, 1, 2, 3, 4};
    uint8_t buf[80] {};
    auto n = frame(0x21, hand, sizeof(hand), buf, sizeof(buf));
    inspector.observe_crc_frame(buf, n, 1, 0x30);
    const uint8_t control[] = {0, 0};
    n = frame(0xCD, control, sizeof(control), buf, sizeof(buf));
    inspector.observe_crc_frame(buf, n, 2, 0x30);
    n = frame(0x55, control, sizeof(control), buf, sizeof(buf));
    inspector.observe_crc_frame(buf, n, 3, 0x30);
    assert(inspector.status().handshake_frames == 1);
    assert(inspector.status().control_frames == 1);
    assert(inspector.status().other_frames == 1);
    assert(inspector.status().sensor_frames == 0);
    Parser::Telemetry value {};
    assert(!inspector.candidate(value, 4));
}

static void decode_valid_candidate_only()
{
    Inspector inspector;
    uint8_t buf[80] {};
    const auto n = esc_frame(buf);
    assert(n == 22);
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    assert(inspector.status().sensor_frames == 1);
    assert(inspector.status().esc_candidates == 1);
    assert(inspector.status().last_sensor_id == 0x20);
    Parser::Telemetry value {};
    assert(inspector.candidate(value, 1050));
    assert(value.valid & Parser::ERPM);
    assert(value.valid & Parser::VOLTAGE);
    assert(value.valid & Parser::CURRENT);
    assert(value.electrical_rpm == 1000);
    assert(value.voltage > 24.99f && value.voltage < 25.01f);
    assert(value.current > 2.49f && value.current < 2.51f);
    assert(!inspector.candidate(value, 1100)); // fail-closed on stale data
}

static void reject_bad_crc()
{
    Inspector inspector;
    uint8_t buf[80] {};
    const auto n = esc_frame(buf);
    buf[14] ^= 0x80;
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    assert(inspector.status().sensor_frames == 0);
    assert(inspector.status().esc_candidates == 0);
}

static void reject_bad_destination_and_length()
{
    Inspector inspector;
    uint8_t buf[80] {};
    auto n = esc_frame(buf, 0x31);
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    assert(inspector.status().rejected_sensors == 1);
    assert(inspector.status().esc_candidates == 0);
    const uint8_t too_short[] = {0x30, 0x20};
    n = frame(0x80, too_short, sizeof(too_short), buf, sizeof(buf));
    inspector.observe_crc_frame(buf, n, 1002, 0x30);
    assert(inspector.status().rejected_sensors == 2);
}

static void distinguish_non_esc_sensor()
{
    Inspector inspector;
    uint8_t buf[80] {};
    const auto n = esc_frame(buf, 0x30, 0x7E);
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    assert(inspector.status().other_sensors == 1);
    assert(inspector.status().esc_candidates == 0);
    assert(inspector.status().last_sensor_id == 0x7E);
}

static void reject_all_missing_measurements()
{
    Inspector inspector;
    uint8_t buf[80] {};
    const auto n = esc_frame(buf, 0x30, 0x20, true);
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    assert(inspector.status().rejected_sensors == 1);
    Parser::Telemetry value {};
    assert(!inspector.candidate(value, 1001));
}

static void invalidate_on_new_frame_or_link_loss()
{
    Inspector inspector;
    uint8_t buf[80] {};
    auto n = esc_frame(buf);
    inspector.observe_crc_frame(buf, n, 1000, 0x30);
    Parser::Telemetry value {};
    assert(inspector.candidate(value, 1001));
    n = esc_frame(buf, 0x31);
    inspector.observe_crc_frame(buf, n, 1002, 0x30);
    assert(!inspector.candidate(value, 1003));
    n = esc_frame(buf);
    inspector.observe_crc_frame(buf, n, 1100, 0x30);
    assert(inspector.candidate(value, 1101));
    inspector.invalidate(); // external link state is no longer LINKED
    assert(!inspector.candidate(value, 1102));
}

int main()
{
    classify_without_tx();
    decode_valid_candidate_only();
    reject_bad_crc();
    reject_bad_destination_and_length();
    distinguish_non_esc_sensor();
    reject_all_missing_measurements();
    invalidate_on_new_frame_or_link_loss();
    std::puts("7 Phase 17E passive-only telemetry observer tests passed (simulation only)");
}
