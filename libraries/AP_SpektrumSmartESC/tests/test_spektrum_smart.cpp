/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC.h"
#include "../AP_SpektrumSmartESC_Handshake.h"
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(value) assert(value)
#define EXPECT_FALSE(value) assert(!(value))
#define EXPECT_EQ(a, b) assert((a) == (b))
#define EXPECT_NEAR(a, b, epsilon) assert(((a) - (b)) < (epsilon) && ((b) - (a)) < (epsilon))
#else
#include <AP_gtest.h>
#endif
#include <cstring>

using Engine = AP_SpektrumSmartESC;

static void seal(uint8_t *packet)
{
    const uint8_t n = packet[2];
    const uint16_t crc = Engine::crc16(packet, n - 2);
    packet[n - 2] = crc >> 8;
    packet[n - 1] = crc;
}

static bool send(Engine &engine, uint8_t *packet, uint32_t now)
{
    bool accepted = false;
    for (uint8_t i = 0; i < packet[2]; i++) {
        accepted = engine.feed(packet[i], now) || accepted;
    }
    return accepted;
}

static bool discover(Engine &engine, uint32_t now = 0)
{
    uint8_t packet[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 1, 4, 0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(packet);
    return send(engine, packet, now);
}

static void telemetry_packet(uint8_t *packet)
{
    const uint8_t fixture[] = {0xA6, 0x80, 22, 0x30,
                              0x20, 0, 0x04, 0xD2, 0x09, 0x60, 0x02, 0x8A,
                              0x04, 0xD2, 0x01, 0x90, 15, 120, 100, 150, 0, 0};
    memcpy(packet, fixture, sizeof(fixture));
    seal(packet);
}

TEST(SpektrumSmart, CrcKnownAnswer)
{
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    EXPECT_EQ(Engine::crc16(data, sizeof(data)), 0x31C3);
    EXPECT_EQ(Engine::crc16(nullptr, 0), 0);
}

TEST(SpektrumSmart, EncodeBounds)
{
    const uint8_t payload[] = {0x40, 0x30, 10, 1, 4, 0x78, 0x56, 0x34, 0x12};
    uint8_t packet[80] {};
    EXPECT_EQ(Engine::encode(0x21, payload, sizeof(payload), packet, sizeof(packet)), 14);
    Engine engine;
    EXPECT_TRUE(send(engine, packet, 0));
    EXPECT_EQ(Engine::encode(0x21, payload, sizeof(payload), packet, 13), 0);
    EXPECT_EQ(Engine::encode(0x21, nullptr, 9, packet, 80), 0);
    EXPECT_EQ(Engine::encode(0x21, payload, 76, packet, 80), 0);
    EXPECT_EQ(Engine::encode(0x21, payload, 9, nullptr, 80), 0);
}

TEST(SpektrumSmart, ChannelDataAndFailsafe)
{
    // Channel mask selects channels 0 and 31, values are little endian.
    const uint8_t payload[] = {0, 0x40, 75, 0x34, 0x12, 1, 0, 0, 0x80, 0, 0x80, 0xFC, 0xFF};
    uint8_t packet[80] {};
    const uint8_t length = Engine::encode(0xCD, payload, sizeof(payload), packet, sizeof(packet));
    Engine::Channels channels {};
    EXPECT_TRUE(Engine::decode_channels(packet, length, channels));
    EXPECT_FALSE(channels.failsafe);
    EXPECT_EQ(channels.reply_id, 0x40);
    EXPECT_EQ(channels.frame_losses, 0x1234);
    EXPECT_EQ(channels.mask, 0x80000001U);
    EXPECT_EQ(channels.values[0], 32768);
    EXPECT_EQ(channels.values[31], 65532);
    EXPECT_EQ(channels.values[1], 0);
    packet[3] = 1;
    seal(packet);
    EXPECT_TRUE(Engine::decode_channels(packet, length, channels));
    EXPECT_TRUE(channels.failsafe);
    packet[8] |= 2; // Mask now requires a missing third channel value.
    seal(packet);
    EXPECT_FALSE(Engine::decode_channels(packet, length, channels));
    EXPECT_FALSE(Engine::decode_channels(nullptr, length, channels));
    EXPECT_FALSE(Engine::decode_channels(packet, length - 1, channels));
}

TEST(SpektrumSmart, StartupDisconnected)
{
    Engine engine;
    Engine::Device device {};
    Engine::Telemetry telemetry {};
    EXPECT_FALSE(engine.device(device, 0));
    engine.update(1000);
    EXPECT_FALSE(engine.device(device, 1000));
    EXPECT_FALSE(engine.telemetry(telemetry, 1000));
}

TEST(SpektrumSmart, HandshakeMetadata)
{
    Engine engine;
    EXPECT_TRUE(discover(engine));
    Engine::Device device {};
    EXPECT_TRUE(engine.device(device, 0));
    EXPECT_EQ(device.id, 0x40);
    EXPECT_EQ(device.priority, 10);
    EXPECT_EQ(device.baud_mask, 1);
    EXPECT_EQ(device.info, 4);
    EXPECT_EQ(device.uid, 0x12345678U);
}

TEST(SpektrumSmart, InvalidCrc)
{
    Engine engine;
    uint8_t packet[22];
    telemetry_packet(packet);
    packet[10] ^= 1;
    EXPECT_FALSE(send(engine, packet, 0));
    EXPECT_EQ(engine.counters().crc_errors, 1U);
    EXPECT_EQ(engine.counters().telemetry, 0U);
}

TEST(SpektrumSmart, TruncatedAndRecovery)
{
    Engine engine;
    engine.feed(0xA6, 0);
    engine.feed(0x21, 0);
    engine.feed(14, 0);
    engine.update(5);
    EXPECT_EQ(engine.counters().incomplete, 1U);
    EXPECT_TRUE(discover(engine, 5));
}

TEST(SpektrumSmart, InvalidLengths)
{
    Engine engine;
    const uint8_t invalid_lengths[] = {0, 4, 81, 255};
    for (uint8_t length : invalid_lengths) {
        engine.feed(0xA6, 0);
        engine.feed(0x21, 0);
        EXPECT_FALSE(engine.feed(length, 0));
    }
    uint8_t short_handshake[] = {0xA6, 0x21, 5, 0, 0};
    seal(short_handshake);
    EXPECT_FALSE(send(engine, short_handshake, 0));
    EXPECT_EQ(engine.counters().length_errors, 5U);
    EXPECT_TRUE(discover(engine));
}

TEST(SpektrumSmart, IncorrectDevice)
{
    Engine engine(0x30, 0x41);
    EXPECT_FALSE(discover(engine));
    EXPECT_EQ(engine.counters().rejected, 1U);
    Engine wrong_class(0x30, 0x30);
    EXPECT_FALSE(discover(wrong_class));
}

TEST(SpektrumSmart, TelemetryRequiresDiscovery)
{
    Engine engine;
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_FALSE(send(engine, packet, 0));
    EXPECT_TRUE(discover(engine));
    packet[3] = 0x31;
    seal(packet);
    EXPECT_FALSE(send(engine, packet, 1));
    EXPECT_EQ(engine.counters().telemetry, 0U);
}

TEST(SpektrumSmart, TelemetryUnits)
{
    Engine engine;
    EXPECT_TRUE(discover(engine));
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_TRUE(send(engine, packet, 1));
    Engine::Telemetry telemetry {};
    EXPECT_TRUE(engine.telemetry(telemetry, 1));
    EXPECT_EQ(telemetry.valid, 511);
    EXPECT_EQ(telemetry.electrical_rpm, 12340U);
    EXPECT_NEAR(telemetry.voltage, 24.0f, 0.001f);
    EXPECT_NEAR(telemetry.temperature_c, 65.0f, 0.001f);
    EXPECT_NEAR(telemetry.current, 12.34f, 0.001f);
    EXPECT_NEAR(telemetry.bec_temperature_c, 40.0f, 0.001f);
    EXPECT_NEAR(telemetry.bec_current, 1.5f, 0.001f);
    EXPECT_NEAR(telemetry.bec_voltage, 6.0f, 0.001f);
    EXPECT_NEAR(telemetry.throttle_pct, 50.0f, 0.001f);
    EXPECT_NEAR(telemetry.power_pct, 75.0f, 0.001f);
}

TEST(SpektrumSmart, UnavailableFields)
{
    uint8_t data[16];
    memset(data, 0xFF, sizeof(data));
    data[0] = 0x20;
    Engine::Telemetry telemetry {};
    EXPECT_TRUE(Engine::decode_esc(data, sizeof(data), telemetry));
    EXPECT_EQ(telemetry.valid, 0);
    EXPECT_FALSE(Engine::decode_esc(nullptr, 16, telemetry));
    EXPECT_FALSE(Engine::decode_esc(data, 15, telemetry));
    data[0] = 0x34;
    EXPECT_FALSE(Engine::decode_esc(data, 16, telemetry));
}

TEST(SpektrumSmart, TimeoutAndRecovery)
{
    Engine engine;
    EXPECT_TRUE(discover(engine));
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_TRUE(send(engine, packet, 1));
    Engine::Telemetry telemetry {};
    EXPECT_TRUE(engine.telemetry(telemetry, 50));
    EXPECT_FALSE(engine.telemetry(telemetry, 51));
    EXPECT_FALSE(send(engine, packet, 51)); // Must observe discovery again.
    EXPECT_TRUE(discover(engine, 52));
    EXPECT_FALSE(engine.telemetry(telemetry, 52));
    EXPECT_TRUE(send(engine, packet, 53));
    EXPECT_TRUE(engine.telemetry(telemetry, 53));
}

TEST(SpektrumSmart, ClockWrap)
{
    Engine engine;
    EXPECT_TRUE(discover(engine, 0xFFFFFFF0U));
    Engine::Device device {};
    EXPECT_TRUE(engine.device(device, 0x10));
    EXPECT_FALSE(engine.device(device, 0x22));
}

TEST(SpektrumSmart, NoiseAndEmbeddedHeader)
{
    Engine engine;
    for (uint16_t i = 0; i < 166; i++) {
        EXPECT_FALSE(engine.feed(uint8_t(i), 0));
    }
    uint8_t packet[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 1, 0, 0xA6, 0, 0, 0, 0, 0};
    seal(packet);
    EXPECT_TRUE(send(engine, packet, 1));
}

TEST(SpektrumSmart, UnsupportedHandshakeCapabilities)
{
    Engine engine;
    uint8_t packet[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 2, 0, 0, 0, 0, 0, 0, 0};
    seal(packet);
    EXPECT_FALSE(send(engine, packet, 0));
    packet[6] = 1;
    packet[5] = 0;
    seal(packet);
    EXPECT_FALSE(send(engine, packet, 1));
    packet[5] = 101;
    seal(packet);
    EXPECT_FALSE(send(engine, packet, 2));
    EXPECT_TRUE(discover(engine, 3));
}

TEST(SpektrumSmart, BadTrafficDoesNotExtendLife)
{
    Engine engine;
    EXPECT_TRUE(discover(engine));
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_TRUE(send(engine, packet, 1));
    packet[20] ^= 1;
    EXPECT_FALSE(send(engine, packet, 40));
    Engine::Telemetry telemetry {};
    EXPECT_FALSE(engine.telemetry(telemetry, 51));
}

TEST(SpektrumSmart, EmptySensorDoesNotFabricateFields)
{
    Engine engine;
    EXPECT_TRUE(discover(engine));
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_TRUE(send(engine, packet, 1));
    memset(packet + 6, 0xFF, 14);
    seal(packet);
    EXPECT_TRUE(send(engine, packet, 2));
    Engine::Telemetry telemetry {};
    EXPECT_TRUE(engine.telemetry(telemetry, 2));
    EXPECT_EQ(telemetry.valid, 0);
}

TEST(SpektrumSmart, EverySingleBitCorruptionRejected)
{
    uint8_t original[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 1, 4, 0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(original);
    for (uint8_t byte = 3; byte < sizeof(original); byte++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            Engine engine;
            uint8_t packet[sizeof(original)];
            memcpy(packet, original, sizeof(original));
            packet[byte] ^= 1U << bit;
            EXPECT_FALSE(send(engine, packet, 0));
            EXPECT_EQ(engine.counters().crc_errors, 1U);
        }
    }
}

TEST(SpektrumSmart, HandshakePlannerListenAndBounds)
{
    AP_SpektrumSmartESC_Handshake plan;
    uint8_t frame[14] {};
    EXPECT_EQ(plan.prepare(1000, frame, sizeof(frame)), 0);
    plan.reset(0, 0x12345678);
    EXPECT_EQ(plan.prepare(49, frame, sizeof(frame)), 0);
    EXPECT_EQ(plan.prepare(50, frame, 13), 0);
    EXPECT_EQ(plan.attempts(), 0);
    EXPECT_EQ(plan.prepare(50, frame, sizeof(frame)), 14);
    EXPECT_EQ(frame[1], 0x21);
    EXPECT_EQ(frame[3], 0x30);
    EXPECT_EQ(frame[4], 0x40);
    EXPECT_EQ(frame[8], 0x78);
    EXPECT_EQ(frame[11], 0x12);
    EXPECT_EQ(Engine::crc16(frame, 14), 0);
}

TEST(SpektrumSmart, HandshakePlannerMockExchange)
{
    AP_SpektrumSmartESC_Handshake plan;
    uint8_t frame[14] {};
    plan.reset(0, 123);
    EXPECT_EQ(plan.prepare(50, frame, sizeof(frame)), 14);
    uint8_t response[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 1, 4, 1, 2, 3, 4, 0, 0};
    seal(response);
    EXPECT_TRUE(plan.observe(response, sizeof(response), 51));
    EXPECT_EQ(plan.prepare(52, frame, sizeof(frame)), 14);
    EXPECT_EQ(frame[4], 0xFF);
    EXPECT_EQ(frame[6], 0); // Agree on 115200, no untested high-baud transition.
    EXPECT_EQ(plan.state(), AP_SpektrumSmartESC_Handshake::State::Complete);
    EXPECT_EQ(plan.prepare(500, frame, sizeof(frame)), 0);
}

TEST(SpektrumSmart, HandshakePlannerRejectsInvalidReplies)
{
    AP_SpektrumSmartESC_Handshake plan;
    uint8_t frame[14] {};
    plan.reset(0, 123);
    plan.prepare(50, frame, sizeof(frame));
    uint8_t response[] = {0xA6, 0x21, 14, 0x41, 0x30, 10, 1, 0, 1, 2, 3, 4, 0, 0};
    seal(response);
    EXPECT_FALSE(plan.observe(response, sizeof(response), 51));
    response[3] = 0x40;
    response[4] = 0x31;
    seal(response);
    EXPECT_FALSE(plan.observe(response, sizeof(response), 51));
    response[4] = 0x30;
    response[6] = 2;
    seal(response);
    EXPECT_FALSE(plan.observe(response, sizeof(response), 51));
    response[6] = 0;
    EXPECT_FALSE(plan.observe(response, sizeof(response), 51)); // Unsealed CRC.
    EXPECT_FALSE(plan.observe(nullptr, 14, 51));
}

TEST(SpektrumSmart, HandshakePlannerTimeoutAndWrap)
{
    AP_SpektrumSmartESC_Handshake plan;
    uint8_t frame[14] {};
    plan.reset(0, 123);
    EXPECT_EQ(plan.prepare(50, frame, sizeof(frame)), 14);
    EXPECT_EQ(plan.prepare(100, frame, sizeof(frame)), 14);
    EXPECT_EQ(plan.prepare(150, frame, sizeof(frame)), 14);
    EXPECT_EQ(plan.prepare(200, frame, sizeof(frame)), 0);
    EXPECT_EQ(plan.state(), AP_SpektrumSmartESC_Handshake::State::TimedOut);
    EXPECT_EQ(plan.attempts(), 3);
    plan.reset(0xFFFFFFF0U, 123);
    EXPECT_EQ(plan.prepare(0x22, frame, sizeof(frame)), 14);
}

TEST(SpektrumSmart, HandshakePlannerYieldsToMaster)
{
    AP_SpektrumSmartESC_Handshake plan;
    uint8_t frame[14] {};
    plan.reset(0, 123);
    plan.prepare(50, frame, sizeof(frame));
    uint8_t master[] = {0xA6, 0x21, 14, 0x21, 0xFF, 10, 0, 1, 1, 2, 3, 4, 0, 0};
    seal(master);
    EXPECT_TRUE(plan.observe(master, sizeof(master), 51));
    EXPECT_EQ(plan.state(), AP_SpektrumSmartESC_Handshake::State::Yielded);
    EXPECT_EQ(plan.prepare(1000, frame, sizeof(frame)), 0);
}

static void passive_discover(Engine &engine)
{
    uint8_t esc[] = {0xA6, 0x21, 14, 0x40, 0x21, 10, 0, 0, 1, 2, 3, 4, 0, 0};
    seal(esc);
    EXPECT_TRUE(send(engine, esc, 0));
    esc[3] = 0x21;
    esc[4] = 0xFF;
    seal(esc);
    EXPECT_TRUE(send(engine, esc, 0));
}

static bool passive_poll(Engine &engine, uint32_t ms, uint8_t reply = 0x40, bool failsafe = false)
{
    uint8_t p[] = {0xA6, 0xCD, 16, uint8_t(failsafe ? 1 : 0), reply, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    seal(p);
    return send(engine, p, ms);
}

TEST(SpektrumSmart, PassiveObserverLearnsReceiverDestination)
{
    Engine engine(0x30, 0x40, 50, true);
    passive_discover(engine);
    uint8_t p[22];
    telemetry_packet(p);
    p[3] = 0x21;
    seal(p);
    EXPECT_FALSE(send(engine, p, 1));
    EXPECT_TRUE(passive_poll(engine, 2));
    EXPECT_TRUE(send(engine, p, 3));
    EXPECT_FALSE(send(engine, p, 4)); // One response per observed poll.
    EXPECT_EQ(engine.counters().polls, 1U);
    EXPECT_EQ(engine.counters().telemetry, 1U);
}

TEST(SpektrumSmart, PassiveObserverRejectsWrongOrLateResponse)
{
    Engine engine(0x30, 0x40, 50, true);
    passive_discover(engine);
    uint8_t p[22];
    telemetry_packet(p);
    p[3] = 0x21;
    seal(p);
    EXPECT_TRUE(passive_poll(engine, 1, 0x41));
    EXPECT_FALSE(send(engine, p, 2));
    EXPECT_TRUE(passive_poll(engine, 3));
    EXPECT_FALSE(send(engine, p, 8));
    EXPECT_TRUE(passive_poll(engine, 9));
    p[3] = 0x30;
    seal(p);
    EXPECT_FALSE(send(engine, p, 10));
}

TEST(SpektrumSmart, PassiveObserverFailsafeWithdrawsTelemetry)
{
    Engine engine(0x30, 0x40, 50, true);
    passive_discover(engine);
    uint8_t p[22];
    telemetry_packet(p);
    p[3] = 0x21;
    seal(p);
    EXPECT_TRUE(passive_poll(engine, 1));
    EXPECT_TRUE(send(engine, p, 2));
    EXPECT_TRUE(passive_poll(engine, 3, 0, true));
    Engine::Telemetry telemetry {};
    EXPECT_FALSE(engine.telemetry(telemetry, 3));
}

TEST(SpektrumSmart, PassiveObserverRequiresNewMasterAfterLoss)
{
    Engine engine(0x30, 0x40, 50, true);
    passive_discover(engine);
    EXPECT_TRUE(passive_poll(engine, 1));
    engine.update(51);
    EXPECT_FALSE(passive_poll(engine, 52));
    engine.reset_observation();
    EXPECT_TRUE(discover(engine, 53)); // ESC reply alone does not establish a master.
    EXPECT_FALSE(passive_poll(engine, 54));
}

TEST(SpektrumSmart, PassiveObserverRejectsHigherNegotiatedBaud)
{
    Engine engine(0x30, 0x40, 50, true);
    passive_discover(engine);
    uint8_t master[] = {0xA6, 0x21, 14, 0x21, 0xFF, 10, 1, 1, 1, 2, 3, 4, 0, 0};
    seal(master);
    EXPECT_FALSE(send(engine, master, 1));
    EXPECT_EQ(engine.counters().unsupported, 1U);
    EXPECT_FALSE(passive_poll(engine, 2));
}

#ifndef SPKTSMART_STANDALONE_TEST
#include "../AP_SpektrumSmartESC_Telem.h"
#if HAL_WITH_ESC_TELEM
const AP_HAL::HAL &hal = AP_HAL::get_HAL();

TEST(SpektrumSmart, PublishToRealEscFrontend)
{
    static AP_ESC_Telem frontend;
    Engine engine;
    EXPECT_TRUE(discover(engine));
    uint8_t packet[22];
    telemetry_packet(packet);
    EXPECT_TRUE(send(engine, packet, 1));
    EXPECT_TRUE(AP_SpektrumSmartESC_Telem::publish(engine, frontend, 0, 1));
    const auto &data = frontend.get_telem_data(0);
    EXPECT_NEAR(data.voltage, 24.0f, 0.001f);
    EXPECT_NEAR(data.current, 12.34f, 0.001f);
    EXPECT_EQ(data.temperature_cdeg, 6500);
    EXPECT_EQ(data.types, AP_ESC_Telem_Backend::VOLTAGE | AP_ESC_Telem_Backend::CURRENT |
              AP_ESC_Telem_Backend::TEMPERATURE);
    const uint16_t count = data.count;
    EXPECT_FALSE(AP_SpektrumSmartESC_Telem::publish(engine, frontend, 0, 51));
    EXPECT_EQ(data.count, count);
    EXPECT_FALSE(AP_SpektrumSmartESC_Telem::publish(engine, frontend, ESC_TELEM_MAX_ESCS, 1));
    packet[10] = 0xFF; // Missing temperature cannot refresh an earlier reading.
    packet[11] = 0xFF;
    seal(packet);
    EXPECT_TRUE(send(engine, packet, 2));
    EXPECT_FALSE(AP_SpektrumSmartESC_Telem::publish(engine, frontend, 0, 2));
    EXPECT_EQ(data.count, count);
    packet[10] = 0x80; // 3276.8 C cannot fit frontend centi-degrees int16.
    packet[11] = 0;
    seal(packet);
    EXPECT_TRUE(send(engine, packet, 3));
    EXPECT_FALSE(AP_SpektrumSmartESC_Telem::publish(engine, frontend, 0, 3));
    float rpm;
    EXPECT_FALSE(frontend.get_rpm(0, rpm));
}
#endif
#endif

TEST(SpektrumSmart, ObservationSeparatesFramingFromDeviceProof)
{
    Engine e;
    uint8_t p[] = {0xA6, 0x21, 14, 0x41, 0x30, 10, 0, 0, 1, 2, 3, 4, 0, 0};
    seal(p); EXPECT_FALSE(send(e, p, 7));
    EXPECT_EQ(e.observation().crc_frames, 1U); EXPECT_EQ(e.observation().accepted, 0U);
    EXPECT_EQ(e.observation().handshake_source, 0x41); EXPECT_EQ(e.observation().type, 0x21);
    Engine::Device d {}; EXPECT_FALSE(e.device(d, 7));
    p[12] ^= 1; EXPECT_FALSE(send(e, p, 8));
    EXPECT_EQ(e.observation().crc_frames, 1U); EXPECT_EQ(e.observation().last_ms, 7U);
    EXPECT_TRUE(discover(e, 10)); EXPECT_EQ(e.observation().accepted, 1U);
    e.update(100); EXPECT_FALSE(e.device(d, 100));
    EXPECT_EQ(e.observation().handshake_source, 0x40); // Historical, not connected status.
}

TEST(SpektrumSmart, ObservationReportsUnsolicitedSensorWithoutPublishing)
{
    Engine e; uint8_t p[22]; telemetry_packet(p);
    EXPECT_FALSE(send(e, p, 0));
    EXPECT_EQ(e.observation().type, 0x80); EXPECT_EQ(e.observation().sensor, 0x20);
    EXPECT_EQ(e.observation().crc_frames, 1U); EXPECT_EQ(e.observation().accepted, 0U);
    Engine::Telemetry t {}; EXPECT_FALSE(e.telemetry(t, 0));
    uint8_t unknown[5]; EXPECT_EQ(Engine::encode(0x99, nullptr, 0, unknown, sizeof(unknown)), 5);
    EXPECT_FALSE(send(e, unknown, 1)); EXPECT_EQ(e.observation().sensor, 0);
    EXPECT_EQ(e.observation().crc_frames, 2U); EXPECT_EQ(e.observation().accepted, 0U);
}

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    CrcKnownAnswer();
    EncodeBounds();
    ChannelDataAndFailsafe();
    StartupDisconnected();
    HandshakeMetadata();
    InvalidCrc();
    TruncatedAndRecovery();
    InvalidLengths();
    IncorrectDevice();
    TelemetryRequiresDiscovery();
    TelemetryUnits();
    UnavailableFields();
    TimeoutAndRecovery();
    ClockWrap();
    NoiseAndEmbeddedHeader();
    UnsupportedHandshakeCapabilities();
    BadTrafficDoesNotExtendLife();
    EmptySensorDoesNotFabricateFields();
    EverySingleBitCorruptionRejected();
    HandshakePlannerListenAndBounds();
    HandshakePlannerMockExchange();
    HandshakePlannerRejectsInvalidReplies();
    HandshakePlannerTimeoutAndWrap();
    HandshakePlannerYieldsToMaster();
    PassiveObserverLearnsReceiverDestination();
    PassiveObserverRejectsWrongOrLateResponse();
    PassiveObserverFailsafeWithdrawsTelemetry();
    PassiveObserverRequiresNewMasterAfterLoss();
    PassiveObserverRejectsHigherNegotiatedBaud();
    ObservationSeparatesFramingFromDeviceProof(); ObservationReportsUnsolicitedSensorWithoutPublishing();
    std::puts("31 Spektrum Smart protocol/planner/observation tests passed");
}
#else
AP_GTEST_MAIN()
#endif
