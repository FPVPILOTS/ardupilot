/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_LabHandshake.h"
#include <cstring>
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#define EXPECT_EQ(a,b) assert((a) == (b))
#else
#include <AP_gtest.h>
#endif
using Reader = AP_SpektrumSmartESC_Reader;
using State = Reader::State;
using Lease = AP_SpektrumSmartESC_LabHandshake::Lease;
static void reply(Reader &r, uint32_t us, uint8_t src = 0x40, uint8_t dst = 0x30, bool bad_crc = false)
{
    const uint8_t payload[] = {src, dst, 10, 1, 0, 1, 2, 3, 4};
    uint8_t p[14];
    EXPECT_EQ(AP_SpektrumSmartESC::encode(0x21, payload, sizeof(payload), p, sizeof(p)), 14);
    p[13] ^= bad_crc;
    for (const uint8_t b : p) { r.feed(b, us); }
}
TEST(SpektrumPowerOn, PreparedDoesNotTransmitAndUsesReferenceVector)
{
    Reader r; uint8_t p[14];
    EXPECT_TRUE(r.start(true, true, 0x40, 0x4156, 0, true));
    EXPECT_EQ(r.status().state, State::PREPARED);
    EXPECT_EQ(r.prepare(1999999, p, 14), 0);
    EXPECT_EQ(r.prepare(2000000, p, 14, false), 0); // Not an assumed power detector.
    EXPECT_EQ(r.prepare(2000000, p, 14), 14);
    // WingFlight profile with UID 0x4156. CRC independently computed by Python
    // binascii.crc_hqx(bytes.fromhex('A6210E30400A000056410000'), 0) == 0x0D5B.
    const uint8_t reference[] = {0xA6,0x21,0x0E,0x30,0x40,0x0A,0,0,0x56,0x41,0,0,0x0D,0x5B};
    EXPECT_EQ(std::memcmp(p, reference, 14), 0);
    EXPECT_EQ(r.status().tx_frames, 0U); // Queuing is not TC.
}
TEST(SpektrumPowerOn, BatteryWindowCoverageWithoutFastOperator)
{
    // Sweep asynchronous power connection across the useful five-second window.
    // A simulated peer listens from boot+50 to boot+250ms; no physical evidence.
    for (uint32_t battery_ms = 2000; battery_ms <= 6700; battery_ms += 37) {
        Reader r; Lease lease; uint8_t p[14];
        EXPECT_TRUE(r.start(true, true, 0x40, 7, 0, true));
        EXPECT_TRUE(lease.start(0x40, 7, 0, true));
        uint32_t last_query = 0;
        // Exercise the real vehicle task's 100-Hz cadence as well as TC timestamps.
        for (uint32_t ms = 0; ms < 7005 && r.active(); ms += 10) {
            const uint8_t n = r.prepare(ms * 1000U, p, 14, ms >= battery_ms);
            if (n == 0) { continue; }
            EXPECT_EQ(n, 14); EXPECT_EQ(p[1], 0x21);
            EXPECT_TRUE(AP_SpektrumSmartESC::crc16(p, n) == 0);
            EXPECT_TRUE(lease.take(p, n, ms));
            EXPECT_TRUE(r.sent((ms + 2) * 1000U));
            if (p[4] == 0x40) {
                EXPECT_TRUE(last_query == 0 || ms - last_query >= 50);
                last_query = ms;
                if (ms >= battery_ms + 50 && ms < battery_ms + 250) {
                    reply(r, (ms + 3) * 1000U);
                }
            }
            ms += 3;
        }
        EXPECT_EQ(r.status().state, State::DISCOVERED);
        EXPECT_EQ(r.status().responses, 1); EXPECT_EQ(r.status().device, 0x40);
        EXPECT_TRUE(r.status().tx_frames <= Reader::MAX_QUERIES + 1U);
        EXPECT_EQ(r.prepare(7000000, p, 14), 0);
    }
}
TEST(SpektrumPowerOn, NoRxTimesOutWithFiniteTraffic)
{
    Reader r; Lease lease; uint8_t p[14]; unsigned queries = 0;
    EXPECT_TRUE(r.start(true, true, 0x40, 7, 0, true));
    EXPECT_TRUE(lease.start(0x40, 7, 0, true));
    for (uint32_t ms = 0; ms <= 7000; ms++) {
        if (r.prepare(ms * 1000U, p, 14)) {
            EXPECT_EQ(p[4], 0x40); EXPECT_EQ(p[1], 0x21); queries++;
            EXPECT_TRUE(lease.take(p, 14, ms)); EXPECT_TRUE(r.sent((ms + 2) * 1000U)); ms += 2;
        }
    }
    r.update(7000000);
    EXPECT_EQ(r.status().state, State::TIMEOUT); EXPECT_EQ(r.status().timeouts, 1U);
    EXPECT_EQ(r.status().rx_bytes, 0U); EXPECT_TRUE(queries > 50 && queries <= Reader::MAX_QUERIES);
    EXPECT_FALSE(lease.active(7000)); EXPECT_EQ(r.prepare(8000000, p, 14), 0);
}
TEST(SpektrumPowerOn, AnnouncementsAndLocalEchoAreNotPeerReplies)
{
    Reader r; uint8_t p[14]; EXPECT_TRUE(r.start(true, true, 0x40, 7, 0, true));
    reply(r, 100000, 0x40, 0); EXPECT_EQ(r.status().responses, 0);
    EXPECT_EQ(r.prepare(2000000, p, 14), 14); EXPECT_TRUE(r.sent(2002000));
    for (const auto b : p) { r.feed(b, 2003000); }
    EXPECT_EQ(r.status().echoes, 1U); EXPECT_EQ(r.status().responses, 0);
    reply(r, 2004000, 0x40, 0); EXPECT_EQ(r.status().state, State::RESPONSE);
    reply(r, 2005000); EXPECT_EQ(r.status().responses, 1);
    EXPECT_EQ(r.prepare(2006000, p, 14), 0); // RX quiet guard.
    EXPECT_EQ(r.prepare(2007000, p, 14), 14); EXPECT_EQ(p[4], 0xFF);
    EXPECT_TRUE(r.sent(2009000)); EXPECT_EQ(r.status().state, State::DISCOVERED);
    EXPECT_EQ(r.prepare(2100000, p, 14), 0);
}
TEST(SpektrumPowerOn, WrongPeerBadCRCAndPartialBytesCannotSucceed)
{
    for (uint8_t fault = 0; fault < 4; fault++) {
        Reader r; uint8_t p[14]; EXPECT_TRUE(r.start(true, true, 0x40, 7, 0, true));
        EXPECT_EQ(r.prepare(2000000, p, 14), 14); EXPECT_TRUE(r.sent(2002000));
        if (fault == 0) { reply(r, 2003000, 0x41); }
        if (fault == 1) { reply(r, 2003000, 0x40, 0x31); }
        if (fault == 2) { reply(r, 2003000, 0x40, 0x30, true); EXPECT_EQ(r.status().crc_errors, 1U); }
        if (fault == 3) { r.feed(0xA6, 2003000); r.feed(0x21, 2003000); }
        EXPECT_EQ(r.status().responses, 0);
        if (fault < 2) { EXPECT_EQ(r.status().state, State::FAULT); }
        r.update(7000000); EXPECT_FALSE(r.active());
        EXPECT_TRUE(r.status().rx_bytes != 0);
    }
}
TEST(SpektrumPowerOn, NativeLeaseEnforcesTimingProfileAndNoMotorPackets)
{
    Lease lease; uint8_t p[14]; Reader r;
    EXPECT_TRUE(lease.start(0x40, 7, 0, true)); EXPECT_TRUE(r.start(true, true, 0x40, 7, 0, true));
    EXPECT_EQ(r.prepare(2000000, p, 14), 14);
    EXPECT_FALSE(lease.take(p, 14, 1999)); EXPECT_TRUE(lease.take(p, 14, 2000));
    EXPECT_FALSE(lease.take(p, 14, 2049)); EXPECT_TRUE(lease.take(p, 14, 2050));
    uint8_t payload[9] {}; EXPECT_EQ(AP_SpektrumSmartESC::encode(0xCD, payload, 9, p, 14), 14);
    EXPECT_FALSE(lease.take(p, 14, 2100));
    EXPECT_EQ(r.prepare(2100000, p, 14), 0); // outstanding TX not acknowledged
    EXPECT_TRUE(r.sent(2002000)); reply(r, 2003000);
    EXPECT_EQ(r.prepare(2100000, p, 14), 14); EXPECT_EQ(p[4], 0xFF);
    EXPECT_TRUE(lease.take(p, 14, 2100)); EXPECT_FALSE(lease.take(p, 14, 2200));
    lease.close(); EXPECT_FALSE(lease.start(0x40, 7, 8000, true));
}
TEST(SpektrumPowerOn, WrapStallAndNoIdleCannotCreateLateTransmission)
{
    const uint32_t start = 0xFFF00000U;
    Reader r; uint8_t p[14]; EXPECT_TRUE(r.start(true, true, 0x40, 7, start, true));
    EXPECT_EQ(r.prepare(start + 2000000U, p, 14, false), 0);
    EXPECT_EQ(r.prepare(start + 6997000U, p, 14), 0);
    EXPECT_EQ(r.status().state, State::TIMEOUT);
    EXPECT_EQ(r.status().tx_frames, 0U);
    EXPECT_EQ(r.prepare(start + 8000000U, p, 14), 0);
    Lease lease; EXPECT_TRUE(lease.start(0x40, 7, 0xFFFFFFF0U, true));
    EXPECT_TRUE(lease.active(0xFFFFFFF0U + 6999)); EXPECT_FALSE(lease.active(0xFFFFFFF0U + 7000));
}
#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    PreparedDoesNotTransmitAndUsesReferenceVector(); BatteryWindowCoverageWithoutFastOperator();
    NoRxTimesOutWithFiniteTraffic(); AnnouncementsAndLocalEchoAreNotPeerReplies();
    WrongPeerBadCRCAndPartialBytesCannotSucceed(); NativeLeaseEnforcesTimingProfileAndNoMotorPackets();
    WrapStallAndNoIdleCannotCreateLateTransmission();
    std::puts("7 power-on handshake sequence tests passed (simulated peer, no hardware)");
}
#else
AP_GTEST_MAIN()
#endif
