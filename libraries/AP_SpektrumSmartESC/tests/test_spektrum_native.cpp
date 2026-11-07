/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_ChannelData.h"
#include "../AP_SpektrumSmartESC_Motor.h"
#include <AP_IOMCU/SmartESCInhibit.h>
#include <cstring>
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#define EXPECT_EQ(a, b) assert((a) == (b))
#else
#include <AP_gtest.h>
#include <AP_RCProtocol/spm_srxl.h>
#include <AP_Math/crc.h>
#include <AP_HAL/UARTDriver.h>
#include <AP_HAL/AP_HAL.h>
const AP_HAL::HAL &hal = AP_HAL::get_HAL();
#endif

using Engine = AP_SpektrumSmartESC;
using Motor = AP_SpektrumSmartESC_Motor;
using Codec = AP_SpektrumSmartESC_ChannelData;

TEST(SpektrumNative, IOProofRequiresExactImmutableCapability)
{
    AP_IOMCU_SmartESCInhibit p;
    EXPECT_FALSE(p.matches(0)); EXPECT_FALSE(p.matches(123));
    p.nonce=123; EXPECT_TRUE(p.matches(123));
    EXPECT_FALSE(AP_IOMCU_SmartESCInhibit::firmware_matches(0,0));
    EXPECT_FALSE(AP_IOMCU_SmartESCInhibit::firmware_matches(123,124));
    EXPECT_TRUE(AP_IOMCU_SmartESCInhibit::firmware_matches(123,123));
    p.mask=0; EXPECT_FALSE(p.matches(123));
    p.mask=4; p.version=2; EXPECT_FALSE(p.matches(123));
    p.version=1; EXPECT_FALSE(p.matches(124));
}
TEST(SpektrumNative, IOResetNonceAndUptime)
{
    AP_IOMCU_SmartESCInhibit a; a.nonce=123; a.uptime_ms=5000;
    auto b=a; b.uptime_ms=5100; EXPECT_TRUE(b.continues(a,123));
    b=a; EXPECT_FALSE(b.continues(a,123)); // Replayed UART response is not fresh IO proof.
    b.uptime_ms=100; EXPECT_FALSE(b.continues(a,123));
    b.nonce=0; EXPECT_FALSE(b.continues(a,123));
    a.uptime_ms=0xFFFFFFF0U; b=a; b.uptime_ms=20;
    EXPECT_TRUE(b.continues(a,123));
}
TEST(SpektrumNative, LabHasNoSRXLPacketSurface)
{
    uint8_t pattern[]={0x55,0xAA,0x55,0xAA};
    EXPECT_TRUE(AP_SpektrumSmartESC_Lab::packet(pattern,4));
    EXPECT_FALSE(AP_SpektrumSmartESC_Lab::packet(pattern,3));
    EXPECT_FALSE(AP_SpektrumSmartESC_Lab::packet(nullptr,4));
    for (unsigned i=0;i<256;i++) {
        pattern[0]=i;
        EXPECT_EQ(AP_SpektrumSmartESC_Lab::packet(pattern,4),i==0x55);
    }
    EXPECT_FALSE(Motor::physical_control_available());
}
TEST(SpektrumNative, ControlWhitelistRejectsOtherChannelsAndCommands)
{
    uint16_t v[32] {}; v[0]=10912; v[6]=54612;
    uint8_t p[80];
    EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0x40,0,0,0x41,v,p,80),18);
    EXPECT_TRUE(Motor::control_packet(p,18));
    EXPECT_FALSE(AP_SpektrumSmartESC_Lab::packet(p,18));
    EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0x40,0,0,3,v,p,80),18);
    EXPECT_FALSE(Motor::control_packet(p,18));
    Codec::encode(Codec::Kind::Failsafe,0x40,0,0,0x41,v,p,80);
    EXPECT_FALSE(Motor::control_packet(p,18)); // Not an approved shutdown semantic.
    Codec::encode(Codec::Kind::Normal,0x40,0,0,0x41,v,p,80); p[12]|=1;
    const auto crc=Engine::crc16(p,16); p[16]=crc>>8; p[17]=crc;
    EXPECT_FALSE(Motor::control_packet(p,18));
}
#ifndef SPKTSMART_STANDALONE_TEST
TEST(SpektrumNative, NativeChannelStructMatchesSparseWireEncoding)
{
    SrxlControlPacket reference {};
    reference.hdr.srxlID=SPEKTRUM_SRXL_ID; reference.hdr.packetType=SRXL_CTRL_ID; reference.hdr.length=18;
    reference.payload.cmd=SRXL_CTRL_CMD_CHANNEL; reference.payload.replyID=0x40;
    reference.payload.channelData.mask=0x41;
    // Wire values are dense and ordered; the reusable codec accepts expanded indexes.
    reference.payload.channelData.values[0]=10912; reference.payload.channelData.values[1]=54612;
    uint16_t expanded[32] {}; expanded[0]=10912; expanded[6]=54612;
    uint8_t p[80]; EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0x40,0,0,0x41,expanded,p,80),18);
    EXPECT_EQ(memcmp(p,&reference,16),0);
    EXPECT_EQ(SRXL_CRC_CALCULATE(p,18,0),0);
    EXPECT_EQ(sizeof(SrxlHandshakePacket),14U); EXPECT_EQ(sizeof(SrxlTelemetryPacket),22U);
}
TEST(SpektrumNative, ProductionCRCUsesNativeArduPilotAlgorithm)
{
    uint8_t data[80];
    for (unsigned i=0;i<80;i++) { data[i]=uint8_t(i*37+17); }
    for (unsigned n=0;n<=80;n++) { EXPECT_EQ(Engine::crc16(data,n),crc16_ccitt(data,n,0)); }
}
TEST(SpektrumNative, IsolatedSmartParserDoesNotReinitializeReceiver)
{
    EXPECT_TRUE(srxlInitDevice(0x31,20,0,0x12345678));
    EXPECT_TRUE(srxlInitBus(0,0,0));
    const auto id=srxlGetDeviceID(0);
    const auto channels=srxlChData;
    const auto telem=srxlTelemData;
    AP_SpektrumSmartESC_Reader smart;
    EXPECT_TRUE(smart.start(true,true,0x40,99,0));
    uint8_t p[80]; EXPECT_EQ(smart.prepare(50000,p,80),14);
    Engine isolated;
    for (unsigned i=0;i<14;i++) { isolated.feed(p[i],51); }
    EXPECT_EQ(srxlGetDeviceID(0),id);
    EXPECT_EQ(memcmp(&srxlChData,&channels,sizeof(channels)),0);
    EXPECT_EQ(memcmp(&srxlTelemData,&telem,sizeof(telem)),0);
}
TEST(SpektrumNative, SerialOptionFourAndTwelveAreDistinct)
{
    EXPECT_EQ(unsigned(AP_HAL::UARTDriver::OPTION_HDPLEX),4U);
    EXPECT_EQ(unsigned(AP_HAL::UARTDriver::OPTION_HDPLEX|AP_HAL::UARTDriver::OPTION_SWAP),12U);
}
#endif

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    IOProofRequiresExactImmutableCapability(); IOResetNonceAndUptime();
    LabHasNoSRXLPacketSurface(); ControlWhitelistRejectsOtherChannelsAndCommands();
    std::puts("4 native boundary/IO/lab policy tests passed (not hardware tests)");
}
#else
AP_GTEST_MAIN()
#endif
