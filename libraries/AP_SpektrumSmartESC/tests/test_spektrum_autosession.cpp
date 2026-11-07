/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_AutoSession.h"
#include <cassert>
#include <cstdio>

using Session = AP_SpektrumSmartESC_AutoSession;
using Auto = AP_SpektrumSmartESC_AutoConnect;
using State = Auto::State;
using Action = Auto::Action;

static void seal(uint8_t *p)
{
    const uint16_t crc = AP_SpektrumSmartESC::crc16(p, p[2] - 2);
    p[p[2] - 2] = crc >> 8;
    p[p[2] - 1] = crc;
}

static void feed_peer(Session &s, uint32_t now_us, bool valid_address = true)
{
    uint8_t p[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 0, 4,
                   0x78, 0x56, 0x34, 0x12, 0, 0};
    if (!valid_address) {
        p[4] = 0x31;
    }
    seal(p);
    for (uint8_t b : p) {
        s.feed(b, now_us);
    }
}

static void handshake_uses_real_reader_frames()
{
    Session s;
    s.enable(0, 0x40, 0xCAFE1234U);
    assert(s.tick(999000, true, true, true) == Action::NONE);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    assert(s.status().state == State::HANDSHAKE);

    uint8_t packet[80] {};
    assert(s.prepare(2999000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(3000000, packet, sizeof(packet), true) == 14);
    assert(packet[0] == 0xA6 && packet[1] == 0x21 &&
           packet[3] == 0x30 && packet[4] == 0x40);
    assert(AP_SpektrumSmartESC::crc16(packet, 14) == 0);
    assert(s.sent(3001500));
    feed_peer(s, 3003000);
    assert(s.reader_status().responses == 1);

    // Reader enforces a 2 ms RX-silence guard before transmitting its
    // confirm broadcast; this is intentional half-duplex protection.
    assert(s.prepare(3004000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(3005000, packet, sizeof(packet), true) == 14);
    assert(packet[1] == 0x21 && packet[4] == 0xFF);
    assert(s.sent(3006500));
    // Native transport must prove RX restored; UART TX complete alone is not
    // sufficient to report a link.
    assert(s.tick(3007000, true, true, false) == Action::NONE);
    assert(s.status().state == State::HANDSHAKE);
    assert(s.tick(3008000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.status().successful == 1);
    assert(s.prepare(3010000, packet, sizeof(packet), true) == 0);

    // A CRC-correct peer announcement maintains freshness, not motor safety.
    uint8_t hello[] = {0xA6, 0x21, 14, 0x40, 0, 10, 0, 4,
                       1, 2, 3, 4, 0, 0};
    seal(hello);
    hello[3] = 0x41; seal(hello);
    assert(!s.observe_peer(hello, 14, 3100));
    hello[3] = 0x40; seal(hello);
    hello[13] ^= 1;
    assert(!s.observe_peer(hello, 14, 3100));
    hello[13] ^= 1;
    assert(s.observe_peer(hello, 14, 3100));
    assert(s.tick(3399000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.tick(3400000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().lost == 1);
    assert(s.prepare(3500000, packet, sizeof(packet), true) == 0);
}

static void wrong_reply_cannot_report_connected()
{
    Session s;
    s.enable(0, 0x40, 123);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    uint8_t p[80] {};
    assert(s.prepare(3000000, p, sizeof(p), true) == 14);
    assert(s.sent(3001500));
    feed_peer(s, 3003000, false);
    assert(s.tick(3004000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().successful == 0);
}

static void no_peer_timeout_then_backoff()
{
    Session s;
    s.enable(0, 0x40, 123);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    // Reader's finite power-on experiment has a 7 s deadline from start,
    // while the policy has a 7.5 s deadline. Crossing the reader deadline
    // must transition to BACKOFF without claiming a peer.
    assert(s.tick(8000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().successful == 0);
}

static void reconnect_repeats_valid_handshake_without_stale_link()
{
    Session s;
    s.enable(0, 0x40, 0x123456U);
    uint8_t packet[80] {};
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    assert(s.prepare(3000000, packet, sizeof(packet), true) == 14);
    assert(s.sent(3001500));
    feed_peer(s, 3003000);
    assert(s.prepare(3005000, packet, sizeof(packet), true) == 14);
    assert(s.sent(3006500));
    assert(s.tick(3007000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.status().successful == 1);

    assert(s.tick(3400000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().lost == 1);
    assert(s.tick(4400000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LISTEN);
    assert(s.tick(5400000, true, true, true) == Action::BEGIN_HANDSHAKE);
    assert(s.prepare(7399000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(7400000, packet, sizeof(packet), true) == 14);
    assert(s.sent(7401500));
    feed_peer(s, 7403000);
    assert(s.prepare(7404000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(7405000, packet, sizeof(packet), true) == 14);
    assert(s.sent(7406500));
    assert(s.tick(7407000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.status().successful == 2);
    assert(s.status().attempts == 2);
    assert(s.status().lost == 1);
}

static void master_conflict_and_unsafe_gate_latch()
{
    Session s;
    s.enable(0, 0x40, 123);
    s.competing_master();
    assert(s.tick(1000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::YIELDED);
    s.enable(2000, 0x40, 123);
    assert(s.tick(3000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    s.uart_fault();
    assert(s.tick(10000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::FAULT);
    s.disable();
    assert(s.tick(20000000, true, true, true) == Action::NONE);
}

int main()
{
    handshake_uses_real_reader_frames();
    wrong_reply_cannot_report_connected();
    no_peer_timeout_then_backoff();
    reconnect_repeats_valid_handshake_without_stale_link();
    master_conflict_and_unsafe_gate_latch();
    std::puts("5 Phase 17B/17C reader auto-session tests passed (simulation only)");
}
