/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Reader.h"
#include <cstring>
#include <initializer_list>
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#define EXPECT_EQ(a, b) assert((a) == (b))
#else
#include <AP_gtest.h>
#include <AP_RCProtocol/spm_srxl.h> // Actual MIT upstream-derived wire structs and CRC macro.
#endif
using Reader = AP_SpektrumSmartESC_Reader;
using State = Reader::State;
using Engine = AP_SpektrumSmartESC;
static void feed(Reader &r, const uint8_t *p, uint8_t n, uint32_t time)
{
    for (uint8_t i = 0; i < n; i++) { r.feed(p[i], time); }
}
static void response(Reader &r, uint32_t time, uint8_t src = 0x40, uint8_t dst = Reader::SOURCE)
{
    uint8_t payload[] = {src, dst, 10, 1, 4, 1, 2, 3, 4}, p[14];
    EXPECT_EQ(Engine::encode(0x21, payload, sizeof(payload), p, sizeof(p)), 14);
    feed(r, p, sizeof(p), time);
}
static void query(Reader &r, uint8_t *p)
{
    EXPECT_TRUE(r.start(true, true, 0x40, 0x11223344, 0));
    EXPECT_EQ(r.prepare(49999, p, 14), 0);
    EXPECT_EQ(r.prepare(50000, p, 14), 14);
    EXPECT_EQ(r.prepare(50001, p, 14), 0);
    EXPECT_TRUE(r.sent(52000));
}
TEST(SpektrumReader, GatesAndDefaults)
{
    Reader r; uint8_t p[14]; EXPECT_FALSE(r.active()); EXPECT_EQ(r.prepare(100000, p, 14), 0);
    EXPECT_FALSE(r.start(false, true, 0x40, 1, 0)); EXPECT_EQ(r.status().state, State::BLOCKED);
    EXPECT_FALSE(r.start(true, false, 0x40, 1, 0)); EXPECT_FALSE(r.start(true, true, 0x30, 1, 0));
    EXPECT_EQ(r.status().tx_frames, 0U);
}
TEST(SpektrumReader, CompleteSingleExchangeAt115200)
{
    Reader r; uint8_t p[14]; query(r, p);
    EXPECT_TRUE(Reader::handshake_packet(p, 14)); EXPECT_EQ(p[3], 0x31); EXPECT_EQ(p[4], 0x40);
    response(r, 54000); EXPECT_EQ(r.status().state, State::CONFIRM_TX);
    EXPECT_EQ(r.status().device, 0x40); EXPECT_EQ(r.status().priority, 10);
    EXPECT_EQ(r.status().baud_mask, 1); EXPECT_EQ(r.status().negotiated_baud, 0U);
    EXPECT_EQ(r.prepare(55000, p, 14), 0); // Never respond inside the RX idle guard.
    EXPECT_EQ(r.prepare(56000, p, 14), 14); EXPECT_EQ(p[4], 0xFF); EXPECT_EQ(p[6], 0);
    EXPECT_TRUE(r.sent(58000)); EXPECT_EQ(r.status().state, State::DISCOVERED);
    EXPECT_EQ(r.status().negotiated_baud, 115200U); EXPECT_EQ(r.status().tx_frames, 2U);
    EXPECT_EQ(r.status().tx_bytes, 28U); EXPECT_EQ(r.prepare(60000, p, 14), 0);
    r.update(1000000); EXPECT_FALSE(r.active()); EXPECT_EQ(r.status().tx_frames, 2U);
}
TEST(SpektrumReader, NoRetriesAndTotalDeadline)
{
    Reader r; uint8_t p[14]; query(r, p); r.update(102000);
    EXPECT_EQ(r.status().state, State::TIMEOUT); EXPECT_EQ(r.prepare(200000, p, 14), 0);
    EXPECT_EQ(r.status().tx_frames, 1U);
    EXPECT_TRUE(r.start(true, true, 0x40, 1, 0));
    EXPECT_EQ(r.prepare(197000, p, 14), 0); EXPECT_EQ(r.status().tx_frames, 0U);
    EXPECT_TRUE(r.start(true, true, 0x40, 1, 0)); r.update(200000);
    EXPECT_EQ(r.status().state, State::TIMEOUT); EXPECT_EQ(r.status().tx_frames, 0U);
}
TEST(SpektrumReader, OwnEchoIsNotPeer)
{
    Reader r; uint8_t p[14]; query(r, p); feed(r, p, 14, 53000);
    EXPECT_EQ(r.status().echoes, 1U); EXPECT_EQ(r.status().device, 0);
    EXPECT_EQ(r.status().state, State::RESPONSE); response(r, 54000);
    EXPECT_EQ(r.status().device, 0x40);
}
TEST(SpektrumReader, WrongIDDestinationOrMasterFailsClosed)
{
    for (uint8_t type = 0; type < 3; type++) {
        Reader r; uint8_t p[14]; query(r, p);
        response(r, 54000, type == 0 ? 0x41 : (type == 1 ? 0x21 : 0x40), type == 2 ? 0x30 : Reader::SOURCE);
        EXPECT_EQ(r.status().state, State::FAULT); EXPECT_EQ(r.status().device, 0);
        EXPECT_EQ(r.prepare(55000, p, 14), 0);
    }
}
TEST(SpektrumReader, BadCRCAndIncompleteDoNotDiscover)
{
    Reader r; uint8_t p[14]; query(r, p);
    p[3] = 0x40; p[13] ^= 1; feed(r, p, 14, 53000);
    EXPECT_EQ(r.status().device, 0); EXPECT_EQ(r.status().crc_errors, 1U);
    feed(r, p, 5, 54000); response(r, 65000);
    EXPECT_EQ(r.status().device, 0x40);
}
TEST(SpektrumReader, RejectAllControlFramesAndBounds)
{
    Reader r; uint8_t p[80]; query(r, p);
    const uint8_t empty_mask[9] {};
    EXPECT_EQ(Engine::encode(0xCD, empty_mask, sizeof(empty_mask), p, sizeof(p)), 14);
    EXPECT_EQ(Engine::crc16(p, 14), 0);
    EXPECT_FALSE(Reader::handshake_packet(p, 14));
    EXPECT_FALSE(Reader::handshake_packet(nullptr, 14));
    EXPECT_FALSE(Reader::handshake_packet(p, 13));
    response(r, 54000);
    EXPECT_EQ(r.prepare(56000, p, 13), 0); EXPECT_EQ(r.status().tx_frames, 1U);
    EXPECT_EQ(r.prepare(56000, p, 14), 14);
}
TEST(SpektrumReader, ClockWrapAndDisabledFeed)
{
    Reader r; uint8_t p[14]; const uint32_t now = 0xFFFFF000U;
    EXPECT_TRUE(r.start(true, true, 0x40, 1, now));
    EXPECT_EQ(r.prepare(now + 50000, p, 14), 14); EXPECT_TRUE(r.sent(now + 52000));
    response(r, now + 54000); EXPECT_EQ(r.status().device, 0x40);
    r.stop(); const auto before = r.status().rx_bytes; response(r, now + 55000);
    EXPECT_EQ(r.status().rx_bytes, before);
}
TEST(SpektrumReader, BufferedBytesBeforeManualRequestCannotExpireAttempt)
{
    for (const uint32_t start : {100000U, 0xFFFFFF00U}) {
        Reader r;
        uint8_t p[14];
        EXPECT_TRUE(r.start(true, true, 0x40, 7, start));
        response(r, start - 1000U);
        EXPECT_EQ(r.status().state, State::WAITING);
        EXPECT_EQ(r.status().stale_bytes, 14U);
        EXPECT_EQ(r.status().rx_bytes, 0U);
        EXPECT_EQ(r.status().responses, 0);
        EXPECT_EQ(r.capture(p, sizeof(p)), 0);
        EXPECT_EQ(r.prepare(start + 50000U, p, sizeof(p)), 14);
        EXPECT_TRUE(r.sent(start + 52000U));
        response(r, start + 54000U);
        EXPECT_EQ(r.status().responses, 1);
        EXPECT_EQ(r.status().state, State::CONFIRM_TX);
    }
}
TEST(SpektrumReader, PeerCounterExcludesEchoAndUnsolicitedAdvertisement)
{
    Reader r;
    uint8_t p[14];
    EXPECT_TRUE(r.start(true, true, 0x40, 0x11223344, 0));
    response(r, 10000, 0x40, 0);
    EXPECT_EQ(r.status().responses, 0);
    EXPECT_EQ(r.prepare(50000, p, sizeof(p)), 14);
    EXPECT_TRUE(r.sent(52000));
    feed(r, p, sizeof(p), 53000);
    EXPECT_EQ(r.status().echoes, 1U);
    EXPECT_EQ(r.status().responses, 0);
    response(r, 54000);
    EXPECT_EQ(r.status().responses, 1);
    EXPECT_EQ(r.status().rx_frames, 3U); // Advertisement, echo, requested peer reply.
}
TEST(SpektrumReader, RawCaptureIsBoundedAndClearedForEachAttempt)
{
    Reader r;
    uint8_t raw[80] {};
    EXPECT_TRUE(r.start(true, true, 0x40, 7, 0));
    for (uint8_t i = 0; i < sizeof(raw); i++) {
        r.feed(i, 10000);
    }
    EXPECT_EQ(r.status().rx_bytes, 80U);
    EXPECT_EQ(r.capture(raw, sizeof(raw)), 64);
    for (uint8_t i = 0; i < 64; i++) {
        EXPECT_EQ(raw[i], i);
    }
    EXPECT_EQ(r.capture(nullptr, 80), 0);
    EXPECT_EQ(r.capture(raw, 7), 7);
    EXPECT_TRUE(r.start(true, true, 0x40, 7, 11000));
    EXPECT_EQ(r.capture(raw, sizeof(raw)), 0);
    EXPECT_EQ(r.status().rx_bytes, 0U);
}
#ifndef SPKTSMART_STANDALONE_TEST
TEST(SpektrumReader, UpstreamLibraryLayoutAndCRCMatch)
{
    Reader r; uint8_t p[14]; query(r, p);
    EXPECT_EQ(sizeof(SrxlHandshakePacket), sizeof(p));
    SrxlHandshakePacket actual; memcpy(&actual, p, sizeof(actual));
    EXPECT_EQ(actual.hdr.packetType, SRXL_HANDSHAKE_ID);
    EXPECT_EQ(actual.payload.srcDevID, Reader::SOURCE); EXPECT_EQ(actual.payload.destDevID, 0x40);
    EXPECT_EQ(actual.payload.uid, 0x11223344U);
    EXPECT_EQ(SRXL_CRC_CALCULATE(p, 12, 0), (uint16_t(p[12]) << 8) | p[13]);
}
AP_GTEST_MAIN()
#else
int main() {
    GatesAndDefaults(); CompleteSingleExchangeAt115200(); NoRetriesAndTotalDeadline(); OwnEchoIsNotPeer();
    WrongIDDestinationOrMasterFailsClosed(); BadCRCAndIncompleteDoNotDiscover();
    RejectAllControlFramesAndBounds(); ClockWrapAndDisabledFeed();
    BufferedBytesBeforeManualRequestCannotExpireAttempt();
    PeerCounterExcludesEchoAndUnsolicitedAdvertisement(); RawCaptureIsBoundedAndClearedForEachAttempt();
    std::puts("11 reader tests passed");
}
#endif
