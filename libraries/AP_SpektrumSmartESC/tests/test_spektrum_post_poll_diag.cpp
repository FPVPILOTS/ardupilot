/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_PostPollDiag.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using Witness = AP_SpektrumSmartESC_PostPollDiag;
using Engine = AP_SpektrumSmartESC;

static uint8_t packet(uint8_t type, uint8_t *out, uint8_t destination = 0x30)
{
    if (type == 0x80) {
        uint8_t payload[17] {};
        payload[0] = destination;
        payload[1] = 0x20;
        return Engine::encode(type, payload, sizeof(payload), out, 80);
    }
    uint8_t payload[9] {};
    payload[0] = 0x40;
    payload[1] = destination;
    return Engine::encode(type, payload, sizeof(payload), out, 80);
}

static void feed_packet(Witness &w, const uint8_t *p, uint8_t n, uint32_t ms)
{
    for (uint8_t i = 0; i < n; i++) {
        w.feed(p[i], ms);
    }
}

static void no_response_and_no_transmitter()
{
    Witness w;
    w.begin(); w.on_true_tc(100); w.finish();
    assert(w.snapshot().tc);
    assert(w.snapshot().bytes == 0 && w.snapshot().sensors == 0);
    assert(!w.active());
}

static void bytes_before_tc_are_not_a_reply()
{
    Witness w; w.begin();
    w.feed(0xA6, 98); w.feed(0x80, 99);
    w.on_true_tc(100);
    w.feed(0, 99);
    w.finish();
    assert(w.snapshot().pre_tc == 3);
    assert(w.snapshot().bytes == 0 && w.snapshot().syncs == 0);
}

static void valid_sensor_at_2ms()
{
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x80, p);
    assert(n == 22);
    w.begin(); w.on_true_tc(100); feed_packet(w, p, n, 102); w.finish();
    const auto &s = w.snapshot();
    assert(s.bytes == 22 && s.bins[0] == 22 && s.sensors == 1);
    assert(s.good_crc == 1 && s.bad_crc == 0 && s.late_sensors == 0);
    assert(s.prefix_len == 8 && s.prefix[0] == 0xA6 && s.prefix[1] == 0x80);
}

static void valid_sensor_at_14ms_is_late()
{
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x80, p);
    w.begin(); w.on_true_tc(100); feed_packet(w, p, n, 114); w.finish();
    assert(w.snapshot().sensors == 1 && w.snapshot().late_sensors == 1);
    assert(w.snapshot().bins[3] == 22);
}

static void late_sensor_after_original_poll_timeout()
{
    // The physical 0xCD transaction may have closed at TC+15 ms,
    // but its independent read-only witness remains active to TC+30 ms.
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x80, p);
    w.begin(); w.on_true_tc(100);
    assert(w.active());
    feed_packet(w, p, n, 125); // 25 ms after TC; not an accepted poll reply.
    w.finish();
    assert(w.snapshot().bytes == 22);
    assert(w.snapshot().sensors == 1 && w.snapshot().late_sensors == 1);
    assert(w.snapshot().bins[3] == 22);
    assert(!w.active());
}

static void crc_failure_and_bad_length()
{
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x80, p);
    w.begin(); w.on_true_tc(100);
    p[n-1] ^= 0x01;
    feed_packet(w, p, n, 101);
    const uint8_t bad[3] = {0xA6, 0x80, 0xFF};
    feed_packet(w, bad, 3, 102);
    w.finish();
    assert(w.snapshot().bad_crc == 1);
    assert(w.snapshot().bad_length == 1);
    assert(w.snapshot().sensors == 0);
}

static void handshake_and_control_are_not_telemetry()
{
    Witness w; uint8_t p[80] {};
    w.begin(); w.on_true_tc(100);
    uint8_t n = packet(0x21, p); feed_packet(w, p, n, 101);
    n = packet(0xCD, p); feed_packet(w, p, n, 102);
    w.finish();
    assert(w.snapshot().handshake == 1 && w.snapshot().control == 1);
    assert(w.snapshot().sensors == 0 && w.snapshot().good_crc == 2);
}

static void timeout_and_truncated_frame()
{
    Witness w; uint8_t p[80] {};
    uint8_t n = packet(0x80, p);
    w.begin(); w.on_true_tc(100);
    feed_packet(w, p, 10, 102);
    feed_packet(w, p, n, 108);
    w.feed(0xA6, 109);
    w.finish();
    assert(w.snapshot().incomplete == 2);
    assert(w.snapshot().sensors == 1);
}

static void capture_next_unsolicited_50ms_heartbeat()
{
    // First handshake after a poll may arrive beyond the former 30ms window.
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x21, p);
    w.begin(); w.on_true_tc(100);
    feed_packet(w, p, n, 150);
    w.finish();
    assert(w.snapshot().bytes == 14);
    assert(w.snapshot().handshake == 1);
    assert(w.snapshot().sensors == 0);
    assert(w.snapshot().bins[3] == 14);
}

static void ignore_late_bytes_after_capture_window()
{
    Witness w; w.begin(); w.on_true_tc(100);
    w.feed(0xA6, 181);
    w.finish();
    assert(w.snapshot().bytes == 0);
}

static void timestamps_wrap()
{
    Witness w; uint8_t p[80] {};
    const uint8_t n = packet(0x80, p);
    w.begin(); w.on_true_tc(0xFFFFFFFEU);
    feed_packet(w, p, n, 0U);
    w.finish();
    assert(w.snapshot().sensors == 1 && w.snapshot().bins[0] == 22);
}

int main()
{
    no_response_and_no_transmitter();
    bytes_before_tc_are_not_a_reply();
    valid_sensor_at_2ms();
    valid_sensor_at_14ms_is_late();
    late_sensor_after_original_poll_timeout();
    crc_failure_and_bad_length();
    handshake_and_control_are_not_telemetry();
    timeout_and_truncated_frame();
    capture_next_unsolicited_50ms_heartbeat();
    ignore_late_bytes_after_capture_window();
    timestamps_wrap();
    std::puts("11 PostPoll RX read-only forensic tests passed");
}
