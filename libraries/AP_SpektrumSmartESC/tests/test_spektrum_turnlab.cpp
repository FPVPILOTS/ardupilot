/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_TurnLabProfile.h"
#include "../../AP_IOMCU/SmartESCInhibit.h"
#include <cassert>
#include <cstdio>
using Turn = AP_SpektrumSmartESC_TurnLab;
// Synthetic codec fixture ONLY. Not Avian qualification or flight settings.
static Turn::Profile fixture()
{
    Turn::Profile p;
    p.channels.throttle_verified = p.channels.reverse_verified = true;
    p.channels.throttle_channel = 0; p.channels.reverse_channel = 6;
    p.channels.zero = 4000; p.channels.full = 64000;
    p.channels.forward = 8000; p.channels.reverse = 56000;
    p.io_crc = 0x12345678; p.electrical = p.loss_behavior = true;
    p.forward_verified = true; // Synthetic FORWARD CH7 reference only.
    return p;
}
static Turn::Evidence evidence(uint32_t now)
{
    Turn::Evidence e;
    e.disarmed = e.emergency_stop = e.owner = e.peer = e.uart_ok = true;
    e.owner_ms = e.peer_ms = e.control_ms = now;
    return e;
}
static void sequence(uint32_t start)
{
    Turn t(fixture());
    assert(t.start(true, evidence(start), start));
    uint8_t packet[18] {};
    for (uint32_t ms = 0; ms < 1500; ms += 20) {
        const uint32_t now = start + ms;
        const auto e = evidence(now);
        assert(t.prepare(e, now, packet, sizeof(packet)) == 18);
        assert(AP_SpektrumSmartESC::crc16(packet, sizeof(packet)) == 0);
        assert(packet[0] == 0xA6 && packet[1] == 0xCD && packet[3] == 0 && packet[4] == 0x40);
        assert(packet[8] == 0x41 && packet[9] == 0 && packet[10] == 0 && packet[11] == 0);
        const uint16_t throttle = uint16_t(packet[12]) | (uint16_t(packet[13]) << 8);
        assert(throttle == (ms >= 500 && ms < 1000 ? 7600 : 4000));
        assert(packet[14] == 0x40 && packet[15] == 0x1F); // CH7 fixed forward 8000.
        assert(t.take(packet, sizeof(packet), e, now));
        assert(t.prepare(e, now, packet, sizeof(packet)) == 0); // In-flight buffer locked.
        assert(t.complete(now + 2, true));
    }
    assert(t.prepare(evidence(start + 1500), start + 1500, packet, sizeof(packet)) == 0);
    assert(t.state() == Turn::State::DONE && t.frames() == 75 && t.completed() == 75);
    assert(!t.start(true, evidence(start + 1600), start + 1600));
}
int main()
{
    unsigned tests = 0;
    sequence(1000); ++tests;
    sequence(0xFFFFFF00U); ++tests; // wrap-safe deadlines.
    uint8_t packet[18] {};
    {
        Turn t(fixture());
        assert(t.prepare(evidence(100), 100, packet, sizeof(packet)) == 0);
        assert(!t.start(false, evidence(100), 100));
        assert(!t.start(true, evidence(101), 101)); ++tests;
    }
    {
        Turn t(spektrum_turnlab_profile());
        assert(!spektrum_turnlab_profile().valid());
        assert(!t.start(true, evidence(100), 100));
        assert(t.encode(Turn::State::FORWARD6, packet, sizeof(packet)) == 0); ++tests;
    }
    for (unsigned fault = 0; fault < 8; fault++) {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        auto e = evidence(120);
        switch (fault) {
        case 0: e.disarmed = false; break;
        case 1: e.owner = false; break;
        case 2: e.uart_ok = false; break;
        case 3: e.emergency_stop = false; break;
        case 4: e.peer = false; break;
        case 5: e.peer_ms = 20; break;
        case 6: e.control_ms = 60; break;
        case 7: e.owner_ms = 70; break;
        }
        assert(t.prepare(e, 120, packet, sizeof(packet)) == 0);
        assert(t.state() == Turn::State::FAULT);
        assert(!t.start(true, evidence(121), 121)); ++tests;
    }
    {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        assert(t.prepare(evidence(160), 160, packet, sizeof(packet)) == 0);
        assert(t.state() == Turn::State::FAULT); ++tests;
    }
    {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        assert(t.prepare(evidence(100), 100, packet, sizeof(packet)) == 18);
        assert(t.take(packet, 18, evidence(100), 100));
        assert(!t.complete(102, false) && t.state() == Turn::State::FAULT); ++tests;
    }
    for (unsigned byte = 0; byte < 18; byte++) {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        assert(t.prepare(evidence(100), 100, packet, sizeof(packet)) == 18);
        packet[byte] ^= 1;
        assert(!t.take(packet, 18, evidence(100), 100));
    }
    ++tests; // Every field including CRC, mask, target, CH1 and CH7 rejected.
    {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        assert(t.encode(Turn::State::FORWARD6, packet, sizeof(packet)) == 18);
        assert(!t.take(packet, 18, evidence(100), 100)); ++tests;
    }
    {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        t.abort(); assert(t.prepare(evidence(101), 101, packet, sizeof(packet)) == 0);
        assert(!t.start(true, evidence(101), 101)); ++tests;
    }
    {
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        assert(t.prepare(evidence(100), 100, packet, sizeof(packet)) == 18);
        assert(t.take(packet, 18, evidence(100), 100)); assert(t.complete(102, true));
        assert(t.prepare(evidence(119), 119, packet, sizeof(packet)) == 0);
        assert(!t.take(nullptr, 18, evidence(120), 120)); ++tests;
    }
    {
        auto p = fixture(); p.channels.reverse_channel = 7;
        assert(!p.valid()); p = fixture(); p.channels.zero = p.channels.full;
        assert(!p.valid()); p = fixture(); p.io_crc = 0; assert(!p.valid()); ++tests;
    }
    {
        // Forward-only TurnLab must NOT demand an independently qualified
        // reverse endpoint: CH7 still carries the verified FORWARD value.
        auto p = fixture();
        p.channels.reverse_verified = false;
        p.channels.reverse = 0; // Deliberately unknown and never transmitted.
        assert(p.valid());
        Turn t(p);
        assert(t.start(true, evidence(100), 100));
        assert(t.prepare(evidence(100), 100, packet, sizeof(packet)) == 18);
        assert(packet[14] == 0x40 && packet[15] == 0x1F); // CH7 = 8000
        assert(t.take(packet, 18, evidence(100), 100));
        assert(t.complete(102, true));
        ++tests;
    }
    {
        auto p = fixture();
        p.forward_verified = false; // No inferred CH7 direction polarity.
        assert(!p.valid());
        Turn t(p);
        assert(!t.start(true, evidence(100), 100));
        ++tests;
    }
    {
        auto p = fixture();
        p.channels.forward = 65535; // Wrong wire quantum, cannot qualify.
        assert(!p.valid());
        p = fixture();
        p.channels.reverse = p.channels.forward; // Unknown reverse is allowed.
        p.channels.reverse_verified = false;
        assert(p.valid());
        ++tests;
    }
    {
        // Independent reference calculation from WingFlight SRXL2-ESC:
        // channel = (pulse_us - 988) * 64, with nominal 1000..2000 us.
        // These are REFERENCE candidates, NOT verified Avian stop/endpoints.
        // Synthetic fixture ONLY; the physical source profile stays blocked.
        const uint16_t candidate_zero = uint16_t((1000U - 988U) * 64U);
        const uint16_t candidate_full = uint16_t((2000U - 988U) * 64U);
        assert(candidate_zero == 768U && candidate_full == 64768U);
        auto p = fixture();
        p.channels.zero = candidate_zero;
        p.channels.full = candidate_full;
        Turn t(p);
        assert(t.six_percent() == 4608U);
        assert(t.encode(Turn::State::NEUTRAL, packet, sizeof(packet)) == Turn::LENGTH);
        assert((uint16_t(packet[12]) | (uint16_t(packet[13]) << 8)) == 768U);
        assert(t.encode(Turn::State::FORWARD6, packet, sizeof(packet)) == Turn::LENGTH);
        assert((uint16_t(packet[12]) | (uint16_t(packet[13]) << 8)) == 4608U);
        assert(t.encode(Turn::State::FINAL_NEUTRAL, packet, sizeof(packet)) == Turn::LENGTH);
        assert((uint16_t(packet[12]) | (uint16_t(packet[13]) << 8)) == 768U);
        assert(!spektrum_turnlab_profile().valid()); // Never authorize hardware.
        ++tests;
    }
    {
        // Dedicated IO companion: channel 3 / index 2 is always absent,
        // while the remaining PWM channels, especially SERVO7, remain intact.
        assert(AP_IOMCU_SmartESCInhibit::permitted_outputs(0xffU) == 0xfbU);
        assert(AP_IOMCU_SmartESCInhibit::permitted_outputs(0x04U) == 0U);
        assert(AP_IOMCU_SmartESCInhibit::permitted_outputs(0x40U) == 0x40U);
        for (uint8_t i = 0; i < 8; i++) {
            assert(AP_IOMCU_SmartESCInhibit::output_allowed(i) == (i != 2));
        }
        ++tests;
    }
    {
        AP_IOMCU_SmartESCInhibit ack;
        ack.nonce = 123; ack.uptime_ms = 100;
        assert(ack.matches(123) && !ack.matches(0) && !ack.matches(124));
        assert(ack.mask == (1U << 2) && (ack.mask & (1U << 6)) == 0);
        assert(!ack.continues(ack, 123)); // frozen/replayed ACK
        auto next = ack; next.uptime_ms = 120; assert(next.continues(ack, 123));
        next.nonce = 0; assert(!next.continues(ack, 123)); // IO reboot
        next = ack; next.uptime_ms = 1; assert(!next.continues(ack, 123));
        next = ack; next.mask |= (1U << 6); assert(!next.matches(123));
        next = ack; next.version++; assert(!next.matches(123)); ++tests;
    }
    {
        assert(!AP_IOMCU_SmartESCInhibit::firmware_matches(0, 0));
        assert(!AP_IOMCU_SmartESCInhibit::firmware_matches(123, 124));
        assert(AP_IOMCU_SmartESCInhibit::firmware_matches(123, 123)); ++tests;
    }
    {
        // Final neutral is never inferred from a previous pulse or an empty mask.
        Turn t(fixture()); assert(t.start(true, evidence(100), 100));
        for (uint32_t ms = 100; ms < 1100; ms += 20) {
            assert(t.prepare(evidence(ms), ms, packet, sizeof(packet)) == 18);
            assert(t.take(packet, 18, evidence(ms), ms)); assert(t.complete(ms + 2, true));
        }
        assert(t.prepare(evidence(1600), 1600, packet, sizeof(packet)) == 0);
        assert(t.state() == Turn::State::FAULT); ++tests;
    }
    std::printf("%u TurnLab sequence/native-whitelist tests passed (synthetic profile; no hardware)\n", tests);
}
