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

// Observe a power-on ESC advertisement while the power-on reader is still
// in its mandatory receive-only preparation period. This evidence must NEVER
// be promoted to a successful directed handshake.
static void startup_advertisement_is_only_evidence()
{
    Session s;
    s.enable(0, 0x40, 0xCAFEBABEU);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    uint8_t hello[] = {0xA6, 0x21, 14, 0x40, 0x00, 10, 0, 4,
                       0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(hello);
    for (uint8_t b : hello) {
        s.feed(b, 1200000);
    }
    const auto &st = s.reader_status();
    assert(st.rx_bytes == 14);
    assert(st.rx_frames == 1);
    assert(st.startup_announcements == 1);
    assert(st.last_hs_src == 0x40 && st.last_hs_dst == 0x00);
    assert(st.last_hs_priority == 10 && st.last_hs_baud == 0);
    assert(st.responses == 0);
    assert(s.status().state == State::HANDSHAKE);
    assert(s.status().successful == 0);
    uint8_t captured_announcement[14] {};
    uint32_t seen_ms = 0;
    assert(s.take_startup_announcement(captured_announcement, sizeof(captured_announcement), seen_ms));
    assert(seen_ms == 1200U);
    assert(captured_announcement[3] == 0x40 && captured_announcement[4] == 0);
    assert(AP_SpektrumSmartESC::crc16(captured_announcement, 14) == 0);
    assert(!s.take_startup_announcement(captured_announcement, sizeof(captured_announcement), seen_ms));
    uint8_t packet[80] {};
    assert(s.prepare(1200000, packet, sizeof(packet), true) == 0);
    // The first directed 0x21 can now be prepared two milliseconds after
    // the CRC-verified startup announcement instead of two seconds later.
    assert(s.prepare(1202000, packet, sizeof(packet), true) == 14);
    assert(packet[3] == 0x30 && packet[4] == 0x40);
    assert(s.sent(1203500));
    assert(s.status().successful == 0);
}

static void malformed_startup_cannot_start_early_tx()
{
    Session s;
    s.enable(0, 0x40, 0xCAFEBABEU);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    uint8_t malformed[] = {0xA6, 0x21, 14, 0x40, 0, 10, 0, 4,
                           0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(malformed);
    malformed[13] ^= 0x01;
    for (uint8_t b : malformed) {
        s.feed(b, 1200000);
    }
    uint8_t captured[14] {};
    uint32_t seen_ms = 0;
    assert(!s.take_startup_announcement(captured, sizeof(captured), seen_ms));
    assert(s.reader_status().startup_announcements == 0);
    uint8_t packet[80] {};
    assert(s.prepare(1202000, packet, sizeof(packet), true) == 0);
    assert(s.status().successful == 0);
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

// FC gets power first, ESC stays absent across the whole first attempt,
// then the ESC is switched on during the second attempt's receive-only
// PREPARED window. A spontaneous 0x40 advertisement can accelerate the
// DIRECTED handshake; it can never by itself establish LINKED.
static void fc_first_esc_late_requires_targeted_reconnect()
{
    Session s;
    s.enable(0, 0x40, 0x5AFEA55AU);
    uint8_t packet[80] {};
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    assert(s.prepare(3000000, packet, sizeof(packet), true) == 14);
    assert(s.sent(3001500)); // physical TC, but no ESC powered yet
    assert(s.tick(8000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().successful == 0);
    assert(s.tick(9000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LISTEN);
    assert(s.tick(10000000, true, true, true) == Action::BEGIN_HANDSHAKE);

    // ESC power-on after FC start, in the new listen-only window.
    uint8_t hello[] = {0xA6, 0x21, 14, 0x40, 0, 10, 0, 4,
                       1, 2, 3, 4, 0, 0};
    seal(hello);
    for (uint8_t b : hello) { s.feed(b, 10500000); }
    assert(s.status().successful == 0);
    uint8_t captured[14] {};
    uint32_t seen_ms = 0;
    assert(s.take_startup_announcement(captured, sizeof(captured), seen_ms));
    assert(seen_ms == 10500U);
    assert(s.prepare(10501000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(10502000, packet, sizeof(packet), true) == 14);
    assert(packet[3] == 0x30 && packet[4] == 0x40);
    assert(s.sent(10503500));
    feed_peer(s, 10504500);
    assert(s.reader_status().responses == 1);
    assert(s.prepare(10507000, packet, sizeof(packet), true) == 14);
    assert(packet[4] == 0xFF);
    assert(s.sent(10508500));
    assert(s.tick(10509000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.status().successful == 1);
    assert(s.status().attempts == 2);
    // A motor packet or a source-less sensor reply is not a valid 0x21 peer.
    uint8_t sensor[] = {0xA6, 0x80, 6, 0x30, 0, 0};
    seal(sensor);
    assert(!s.observe_peer(sensor, sizeof(sensor), 10600));
}

static void esc_first_with_no_reannouncement_still_polls()
{
    Session s;
    s.enable(0, 0x40, 0x13572468);
    uint8_t packet[80] {};
    // ESC was already powered, so there is no new 0x40->0x00 announce.
    // FC must query at the end of its bounded listen-only period.
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    assert(s.prepare(2999000, packet, sizeof(packet), true) == 0);
    assert(s.prepare(3000000, packet, sizeof(packet), true) == 14);
    assert(packet[3] == 0x30 && packet[4] == 0x40);
    assert(s.sent(3001500));
    feed_peer(s, 3003000);
    assert(s.prepare(3005000, packet, sizeof(packet), true) == 14);
    assert(packet[4] == 0xFF);
    assert(s.sent(3006500));
    assert(s.tick(3007000, true, true, false) == Action::NONE);
    assert(s.status().state == State::HANDSHAKE);
    assert(s.tick(3008000, true, true, true) == Action::NONE);
    assert(s.status().state == State::LINKED);
    assert(s.status().successful == 1);
}

// On the real FC-only reboot at 20:21 the already powered Avian sent
// two CRC-valid 0x40->0x00 announcements, not a requested 0x40->0x30
// response. Record their actual timing, never promote them to LINKED,
// and do not assume more 0x21 transmissions are accepted by the ESC.
static void fc_reboot_receives_two_d00_but_no_d30()
{
    Session s;
    s.enable(0, 0x40, 0x1234U);
    assert(s.tick(1000000, true, true, true) == Action::BEGIN_HANDSHAKE);
    uint8_t announce[] = {0xA6, 0x21, 14, 0x40, 0x00, 10, 0, 4,
                          0x12, 0x34, 0x56, 0x78, 0, 0};
    seal(announce);
    for (uint8_t b : announce) { s.feed(b, 1050000); }
    assert(s.reader_status().startup_announcements == 1);
    assert(s.reader_status().first_announcement_ms == 50U);
    uint8_t packet[80] {};
    assert(s.prepare(1052000, packet, sizeof(packet), true) == 14);
    assert(packet[3] == 0x30 && packet[4] == 0x40);
    assert(s.sent(1053500)); // Native UART TX complete.
    assert(s.reader_status().first_targeted_tc_ms == 53U);
    for (uint8_t b : announce) { s.feed(b, 1100000); }
    const auto &st = s.reader_status();
    assert(st.startup_announcements == 2);
    assert(st.last_announcement_ms == 100U);
    assert(st.responses == 0);
    assert(st.last_targeted_reply_ms == 0);
    assert(s.status().successful == 0);
    assert(s.tick(1110000, true, true, true) == Action::NONE);
    assert(s.prepare(1112000, packet, sizeof(packet), true) == 14);
    assert(s.sent(1113500));
    assert(s.reader_status().last_targeted_tc_ms == 113U);
    assert(s.tick(8000000, true, true, true) == Action::NONE);
    assert(s.status().state == State::BACKOFF);
    assert(s.status().successful == 0);
    assert(s.status().reason == Auto::Reason::HANDSHAKE_TIMEOUT);
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
    startup_advertisement_is_only_evidence();
    malformed_startup_cannot_start_early_tx();
    wrong_reply_cannot_report_connected();
    no_peer_timeout_then_backoff();
    reconnect_repeats_valid_handshake_without_stale_link();
    master_conflict_and_unsafe_gate_latch();
    fc_first_esc_late_requires_targeted_reconnect();
    esc_first_with_no_reannouncement_still_polls();
    fc_reboot_receives_two_d00_but_no_d30();
    std::puts("10 automatic SRXL2 boot/reconnect auto-session tests passed (simulation only)");
}
