/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_LabHandshake.h"
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
using Lab = AP_SpektrumSmartESC_LabHandshake;
static void packet(uint8_t *p, uint8_t dest, uint32_t uid = 7)
{
    const uint8_t payload[] = {0x31, dest, 20, 0, 0, uint8_t(uid), uint8_t(uid >> 8),
                              uint8_t(uid >> 16), uint8_t(uid >> 24)};
    EXPECT_EQ(AP_SpektrumSmartESC::encode(0x21, payload, sizeof(payload), p, 14), 14);
}
TEST(SpektrumHSLab, AcknowledgementIsExplicitAndExpires)
{
    Lab::Authorization a;
    EXPECT_FALSE(a.consume(true, 100));
    EXPECT_FALSE(a.acknowledge(1, true, 100));
    EXPECT_FALSE(a.acknowledge(15, false, 100));
    EXPECT_TRUE(a.acknowledge(15, true, 100));
    EXPECT_TRUE(a.ready(10099)); EXPECT_FALSE(a.ready(10100));
    EXPECT_FALSE(a.consume(true, 10100));
}
TEST(SpektrumHSLab, CancelAndOneAttemptPerBoot)
{
    Lab::Authorization a;
    EXPECT_TRUE(a.acknowledge(15, true, 100)); a.cancel(); EXPECT_FALSE(a.consume(true, 101));
    EXPECT_TRUE(a.acknowledge(15, true, 102)); EXPECT_FALSE(a.consume(false, 103));
    EXPECT_TRUE(a.acknowledge(15, true, 104)); EXPECT_TRUE(a.consume(true, 105));
    EXPECT_TRUE(a.used()); EXPECT_FALSE(a.acknowledge(15, true, 20000));
    EXPECT_FALSE(a.consume(true, 20001));
}
TEST(SpektrumHSLab, HardwareLeaseCannotStartWithoutAuthorizationCall)
{
    Lab::Lease lease; uint8_t p[14]; packet(p, 0x40);
    EXPECT_FALSE(lease.active(0)); EXPECT_FALSE(lease.take(p, sizeof(p), 0));
    EXPECT_FALSE(lease.start(0x30, 7, 0)); EXPECT_TRUE(lease.start(0x40, 7, 0));
    EXPECT_TRUE(lease.take(p, sizeof(p), 50)); EXPECT_EQ(lease.frames(), 1);
    lease.close(); EXPECT_FALSE(lease.take(p, sizeof(p), 51));
    EXPECT_FALSE(lease.start(0x40, 7, 20000));
}
TEST(SpektrumHSLab, QueryThenBroadcastAndNeverAThirdFrame)
{
    Lab::Lease lease; uint8_t p[14]; EXPECT_TRUE(lease.start(0x40, 7, 0));
    packet(p, 0xFF); EXPECT_FALSE(lease.take(p, 14, 50));
    packet(p, 0x41); EXPECT_FALSE(lease.take(p, 14, 50));
    packet(p, 0x40, 8); EXPECT_FALSE(lease.take(p, 14, 50));
    packet(p, 0x40); EXPECT_TRUE(lease.take(p, 14, 50));
    EXPECT_FALSE(lease.take(p, 14, 60));
    packet(p, 0xFF); EXPECT_TRUE(lease.take(p, 14, 60));
    EXPECT_FALSE(lease.take(p, 14, 70)); EXPECT_EQ(lease.frames(), 2);
}
TEST(SpektrumHSLab, NoChannelPacketEvenWithValidCRC)
{
    Lab::Lease lease; uint8_t p[14]; EXPECT_TRUE(lease.start(0x40, 7, 0));
    const uint8_t empty_channels[9] {};
    EXPECT_EQ(AP_SpektrumSmartESC::encode(0xCD, empty_channels, sizeof(empty_channels), p, 14), 14);
    EXPECT_FALSE(lease.take(p, 14, 50));
    packet(p, 0x40); p[13] ^= 1; EXPECT_FALSE(lease.take(p, 14, 50));
    EXPECT_FALSE(lease.take(nullptr, 14, 50)); EXPECT_EQ(lease.frames(), 0);
}
TEST(SpektrumHSLab, DeadlineReserveAndClockWrap)
{
    const uint32_t origins[] = {0, 0xFFFFFFF0U};
    for (const uint32_t start : origins) {
        Lab::Authorization a; EXPECT_TRUE(a.acknowledge(15, true, start));
        EXPECT_TRUE(a.consume(true, start + 1));
        Lab::Lease lease; uint8_t p[14]; packet(p, 0x40);
        EXPECT_TRUE(lease.start(0x40, 7, start)); EXPECT_TRUE(lease.active(start + 199));
        EXPECT_FALSE(lease.take(p, 14, start + 197)); EXPECT_FALSE(lease.active(start + 200));
        EXPECT_FALSE(lease.take(p, 14, start + 200));
    }
}
#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    AcknowledgementIsExplicitAndExpires(); CancelAndOneAttemptPerBoot();
    HardwareLeaseCannotStartWithoutAuthorizationCall(); QueryThenBroadcastAndNeverAThirdFrame();
    NoChannelPacketEvenWithValidCRC(); DeadlineReserveAndClockWrap();
    std::puts("6 handshake LAB authorization/lease tests passed");
}
#else
AP_GTEST_MAIN()
#endif
