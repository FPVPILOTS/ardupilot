/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_PollLab.h"
#include "../AP_SpektrumSmartESC_Reader.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using Lab = AP_SpektrumSmartESC_PollLab;

static void exact_preview_only()
{
    uint8_t p[80] {};
    const uint8_t n = Lab::encode(p, sizeof(p));
    assert(n == 14);
    assert(Lab::exact_packet(p, n));
    assert(p[0] == 0xA6 && p[1] == 0xCD && p[4] == 0x40);
    assert(AP_SpektrumSmartESC::crc16(p, n) == 0);
    AP_SpektrumSmartESC::Channels c {};
    assert(AP_SpektrumSmartESC::decode_channels(p, n, c));
    assert(!c.failsafe && c.reply_id == 0x40 && c.mask == 0);
    assert(!AP_SpektrumSmartESC_Reader::handshake_packet(p, n));
}

static void reject_every_mutated_control_payload_byte()
{
    uint8_t p[14] {};
    assert(Lab::encode(p, sizeof(p)) == sizeof(p));
    for (uint8_t pos = 0; pos < sizeof(p) - 2; pos++) {
        p[pos] ^= 1;
        assert(!Lab::exact_packet(p, sizeof(p)));
        p[pos] ^= 1;
    }
    // A malicious caller cannot re-sign a 0xCD packet containing channel
    // values, a changed replyID, or different RSSI/failsafe metadata.
    p[4] = 0x41;
    const uint16_t crc = AP_SpektrumSmartESC::crc16(p, 12);
    p[12] = uint8_t(crc >> 8);
    p[13] = uint8_t(crc);
    assert(!Lab::exact_packet(p, sizeof(p)));
}

static void corrupted_crc_never_accepted()
{
    uint8_t p[14] {};
    assert(Lab::encode(p, sizeof(p)) == sizeof(p));
    p[13] ^= 1;
    assert(!Lab::exact_packet(p, sizeof(p)));
}

static void refuse_wrong_length_and_null()
{
    uint8_t p[14] {};
    assert(Lab::encode(p, sizeof(p)) == sizeof(p));
    assert(!Lab::exact_packet(nullptr, sizeof(p)));
    assert(!Lab::exact_packet(p, 13));
    assert(Lab::encode(p, 13) == 0);
}

static void one_shot_native_lease_no_rearm()
{
    Lab::Lease l;
    uint8_t p[14] {};
    assert(Lab::encode(p, sizeof(p)) == sizeof(p));
    assert(!l.take(p, sizeof(p), 10));
    assert(l.start(100));
    assert(l.used());
    assert(l.active(100));
    assert(!l.start(101));
    assert(l.take(p, sizeof(p), 102));
    assert(!l.take(p, sizeof(p), 103));
    l.close();
    assert(!l.active(104));
    assert(!l.start(2000));
}

static void lease_expires_before_tx()
{
    Lab::Lease l;
    uint8_t p[14] {};
    assert(Lab::encode(p, sizeof(p)) == sizeof(p));
    assert(l.start(100));
    assert(!l.active(200));
    assert(!l.take(p, sizeof(p), 200));
}

int main()
{
    exact_preview_only();
    reject_every_mutated_control_payload_byte();
    corrupted_crc_never_accepted();
    refuse_wrong_length_and_null();
    one_shot_native_lease_no_rearm();
    lease_expires_before_tx();
    std::puts("6 Phase 17E-D one-shot 0xCD native whitelist/lease tests passed (host only)");
}
