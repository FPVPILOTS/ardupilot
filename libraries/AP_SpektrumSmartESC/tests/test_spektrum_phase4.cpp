/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Bus.h"
#include "../AP_SpektrumSmartESC_HandshakeBench.h"
#include "../AP_SpektrumSmartESC_Commands.h"
#include "../AP_SpektrumSmartESC_Measurements.h"
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#define EXPECT_EQ(a, b) assert((a) == (b))
#define EXPECT_NEAR(a, b, e) assert(((a)-(b)) < (e) && ((b)-(a)) < (e))
#else
#include <AP_gtest.h>
#endif
#include <cstring>
#include <cmath>

using Bus = AP_SpektrumSmartESC_Bus;
using State = Bus::State;
using Engine = AP_SpektrumSmartESC;
using Commands = AP_SpektrumSmartESC_Commands;
using Measurements = AP_SpektrumSmartESC_Measurements;

class MockPort : public Bus::Port {
public:
    bool real = false, high = true, hit = false, done = false, release_ok = true;
    bool owned = false, driving = false;
    unsigned sent = 0, released = 0;
    const uint8_t *buffer = nullptr;
    uint8_t length = 0;
    bool acquire() override { if (owned) { return false; } owned = true; return true; }
    void relinquish() override { owned = false; }
    bool simulated() const override { return !real; }
    bool idle_high() const override { return high; }
    bool collision() const override { return hit; }
    bool begin_tx(const uint8_t *p, uint8_t n) override {
        if (!owned || driving) { return false; }
        buffer = p; length = n; driving = true; done = false; sent++; return true;
    }
    bool transmission_complete() const override { return done; }
    bool release_rx() override {
        released++;
        if (!release_ok) { return false; }
        driving = false; return true;
    }
};

static Bus::Gate verified_mock_gate()
{
    Bus::Gate g;
    g.electrical_verified = g.controller_role_verified = g.esc_initialization_verified = true;
    return g;
}

static void seal(uint8_t *p)
{
    const uint16_t crc = Engine::crc16(p, p[2] - 2);
    p[p[2]-2] = crc >> 8; p[p[2]-1] = crc;
}

static bool reply(Bus &b, uint32_t now, uint8_t source = 0x40)
{
    uint8_t p[] = {0xA6, 0x21, 14, source, 0x30, 10, 1, 4, 0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(p);
    return b.observe(p, sizeof(p), now);
}

static void connected(Bus &b, MockPort &p)
{
    b.start(0, 123);
    b.update(50000);
    EXPECT_EQ(b.state(), State::TX_ACTIVE);
    EXPECT_EQ(p.length, 14);
    EXPECT_EQ(Engine::crc16(p.buffer, p.length), 0);
    p.done = true; b.update(51220);
    EXPECT_EQ(b.state(), State::WAITING_FOR_RESPONSE);
    EXPECT_FALSE(p.driving);
    EXPECT_TRUE(reply(b, 52000));
    EXPECT_EQ(b.state(), State::HANDSHAKE_PENDING);
    b.update(52173); EXPECT_EQ(b.state(), State::HANDSHAKE_PENDING);
    b.update(52174); EXPECT_EQ(b.state(), State::TX_ACTIVE);
    EXPECT_EQ(p.buffer[4], 0xFF);
    p.done = true; b.update(53400);
    EXPECT_EQ(b.state(), State::CONNECTED);
    EXPECT_FALSE(p.driving);
}

TEST(SpektrumPhase4, DefaultAndHardwareGateClosed)
{
    MockPort p; Bus::Gate gate;
    Bus b(p, gate);
    EXPECT_EQ(b.state(), State::DISABLED);
    b.start(0, 1); b.update(1000000);
    EXPECT_EQ(b.state(), State::PASSIVE_RX); EXPECT_EQ(p.sent, 0U);
    MockPort hardware; hardware.real = true;
    Bus h(hardware, verified_mock_gate()); h.start(0, 1); h.update(1000000);
    EXPECT_EQ(h.state(), State::PASSIVE_RX); EXPECT_EQ(hardware.sent, 0U);
}

TEST(SpektrumPhase4, DiscoveryTurnaroundAndOwnership)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_EQ(p.sent, 2U);
    b.disable(); EXPECT_FALSE(p.owned); EXPECT_FALSE(p.driving);
    EXPECT_EQ(b.state(), State::DISABLED);
}

TEST(SpektrumPhase4, PhysicalTCRequiredAndBufferStable)
{
    MockPort p; Bus b(p, verified_mock_gate()); b.start(0, 1); b.update(50000);
    uint8_t copy[14]; memcpy(copy, p.buffer, sizeof(copy));
    b.update(51000);
    EXPECT_EQ(b.state(), State::TX_ACTIVE); EXPECT_TRUE(p.driving);
    EXPECT_EQ(memcmp(copy, p.buffer, sizeof(copy)), 0);
    EXPECT_FALSE(b.request_telemetry(51001, true));
    p.done = true; b.update(51220); EXPECT_FALSE(p.driving);
}

TEST(SpektrumPhase4, CollisionReleasesBus)
{
    MockPort p; Bus b(p, verified_mock_gate()); b.start(0, 1); b.update(50000);
    p.hit = true; b.update(50001);
    EXPECT_EQ(b.state(), State::FAULT); EXPECT_FALSE(p.driving);
    EXPECT_EQ(b.counters().collisions, 1U);
}

TEST(SpektrumPhase4, ReceiveDuringTransmitIsCollision)
{
    MockPort p; Bus b(p, verified_mock_gate()); b.start(0, 1); b.update(50000);
    EXPECT_FALSE(reply(b, 50010)); EXPECT_EQ(b.state(), State::FAULT);
    EXPECT_FALSE(p.driving);
}

TEST(SpektrumPhase4, StuckLowAndStuckTransmitter)
{
    MockPort p; p.high = false; Bus b(p, verified_mock_gate()); b.start(0, 1);
    b.update(200000); EXPECT_EQ(b.state(), State::FAULT); EXPECT_EQ(p.sent, 0U);
    p.high = true; b.start(300000, 1); b.update(350000); b.update(353000);
    EXPECT_EQ(b.state(), State::FAULT); EXPECT_FALSE(p.driving);
}

TEST(SpektrumPhase4, MissingESCBoundedRetries)
{
    MockPort p; Bus b(p, verified_mock_gate()); b.start(0, 1);
    for (uint32_t t = 50000; t <= 152440; t += 51220) {
        b.update(t); p.done = true; b.update(t + 1220);
    }
    b.update(203660);
    EXPECT_EQ(p.sent, 3U); EXPECT_EQ(b.state(), State::COMMUNICATION_LOST);
    b.update(1000000); EXPECT_EQ(p.sent, 3U);
}

TEST(SpektrumPhase4, BadCRCAndUnexpectedMaster)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    uint8_t bad[] = {0xA6, 0x80, 5, 0, 0};
    EXPECT_FALSE(b.observe(bad, sizeof(bad), 54000)); EXPECT_EQ(b.counters().crc, 1U);
    EXPECT_FALSE(reply(b, 55000, 0x20)); EXPECT_EQ(b.state(), State::PASSIVE_RX);
    b.update(1000000); EXPECT_EQ(p.sent, 2U);
}

TEST(SpektrumPhase4, RecoveryRequiresExplicitRestart)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    b.update(103400); EXPECT_EQ(b.state(), State::COMMUNICATION_LOST);
    EXPECT_FALSE(reply(b, 103500)); b.update(200000); EXPECT_EQ(p.sent, 2U);
    b.start(200000, 42); b.update(249999); EXPECT_EQ(b.state(), State::BUS_IDLE);
    b.update(250000); EXPECT_EQ(b.state(), State::TX_ACTIVE);
}

TEST(SpektrumPhase4, TelemetryPollFreshnessAndTiming)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_FALSE(b.request_telemetry(54000, false));
    EXPECT_TRUE(b.request_telemetry(54000, true));
    p.done = true; b.update(55300);
    uint8_t t[] = {0xA6, 0x80, 22, 0x30, 0x20, 0, 0x04, 0xD2, 0x09, 0x60, 0x02, 0x8A,
                   0x04, 0xD2, 0x01, 0x90, 15, 120, 100, 150, 0, 0};
    seal(t); EXPECT_TRUE(b.observe(t, sizeof(t), 56000));
    EXPECT_FALSE(b.observe(t, sizeof(t), 56100)); // Unsolicited duplicate cannot refresh data.
    Engine::Telemetry result;
    EXPECT_TRUE(b.telemetry(result, 105999)); EXPECT_FALSE(b.telemetry(result, 106000));
    EXPECT_EQ(b.counters().telemetry, 1U);
    EXPECT_TRUE(b.request_telemetry(57000, true)); p.done = true; b.update(58300);
    EXPECT_FALSE(b.observe(t, sizeof(t), 63300)); b.update(63300);
    EXPECT_EQ(b.state(), State::COMMUNICATION_LOST);
}

TEST(SpektrumPhase4, ReleaseFailureAndExclusiveOwner)
{
    MockPort p; p.owned = true; Bus b(p, verified_mock_gate()); b.start(0, 1);
    EXPECT_EQ(b.state(), State::FAULT); EXPECT_EQ(p.released, 0U);
    p.owned = false; b.start(0, 1); b.update(50000); p.release_ok = false;
    p.done = true; b.update(51220); EXPECT_EQ(b.state(), State::FAULT);
    EXPECT_TRUE(p.owned); // A failed release must not advertise the port as available.
    p.release_ok = true; b.disable(); EXPECT_FALSE(p.owned);
}

TEST(SpektrumPhase4, ClockWrapAndUnexpectedDevice)
{
    MockPort p; Bus b(p, verified_mock_gate()); const uint32_t start = 0xFFFF8000U;
    b.start(start, 1); b.update(start + 50000U); EXPECT_EQ(b.state(), State::TX_ACTIVE);
    p.done = true; b.update(start + 51220U);
    EXPECT_FALSE(reply(b, start + 52000U, 0x41)); EXPECT_EQ(b.state(), State::PASSIVE_RX);
}

// Synthetic calibration values are deliberately not claimed to be Avian settings.
static Commands::Profile profile(bool reverse = false)
{
    Commands::Profile p; p.throttle_verified = true; p.throttle_channel = 0;
    p.zero = 1000; p.full = 61000; p.reverse_verified = reverse;
    p.reverse_channel = 6; p.forward = 2000; p.reverse = 60000; p.direction_dwell_ms = 100;
    return p;
}
static Commands::Demand ready()
{
    Commands::Demand d; d.armed = d.rc_ok = d.esc_ok = d.stopped = true; return d;
}

TEST(SpektrumPhase4, CommandsRequireCalibrationAndNeutral)
{
    uint8_t p[80]; Commands::Profile unknown; Commands c(unknown); auto d = ready();
    EXPECT_EQ(c.prepare(d, 0, p, sizeof(p)), 0);
    Commands calibrated(profile()); d.throttle = 1;
    EXPECT_EQ(calibrated.prepare(d, 0, p, sizeof(p)), 0);
    d.throttle = 0; d.stopped = false; EXPECT_EQ(calibrated.prepare(d, 1, p, sizeof(p)), 0);
    d.stopped = true; EXPECT_EQ(calibrated.prepare(d, 2, p, sizeof(p)), 16);
    Engine::Channels decoded; EXPECT_TRUE(Engine::decode_channels(p, 16, decoded));
    EXPECT_EQ(decoded.values[0], 1000); EXPECT_EQ(decoded.reply_id, 0);
}

TEST(SpektrumPhase4, ForwardMappingAndBounds)
{
    Commands c(profile()); auto d = ready(); uint8_t p[80]; c.prepare(d, 0, p, sizeof(p));
    d.throttle = 0.5f; EXPECT_EQ(c.prepare(d, 1, p, sizeof(p)), 16);
    Engine::Channels decoded; EXPECT_TRUE(Engine::decode_channels(p, 16, decoded));
    EXPECT_EQ(decoded.values[0], 31000);
    d.throttle = 1; c.prepare(d, 2, p, sizeof(p)); Engine::decode_channels(p, 16, decoded);
    EXPECT_EQ(decoded.values[0], 61000);
    d.throttle = NAN; EXPECT_EQ(c.prepare(d, 3, p, sizeof(p)), 0);
    d.throttle = 1.1f; EXPECT_EQ(c.prepare(d, 4, p, sizeof(p)), 0);
    d.throttle = -0.1f; EXPECT_EQ(c.prepare(d, 5, p, sizeof(p)), 0);
}

TEST(SpektrumPhase4, DisarmRCLossESCLossAndFailsafe)
{
    for (unsigned reason = 0; reason < 4; reason++) {
        Commands c(profile()); auto d = ready(); uint8_t p[80]; c.prepare(d, 0, p, sizeof(p));
        d.throttle = 0.7f;
        if (reason == 0) { d.armed = false; }
        if (reason == 1) { d.rc_ok = false; }
        if (reason == 2) { d.esc_ok = false; }
        if (reason == 3) { d.failsafe = true; }
        EXPECT_EQ(c.prepare(d, 1, p, sizeof(p)), 0);
        d = ready(); d.throttle = 0.7f;
        EXPECT_EQ(c.prepare(d, 2, p, sizeof(p)), 0);
        d.throttle = 0; EXPECT_EQ(c.prepare(d, 3, p, sizeof(p)), 16);
    }
}

TEST(SpektrumPhase4, ReverseRequiresStoppedDwell)
{
    Commands c(profile(true)); auto d = ready(); uint8_t p[80]; c.prepare(d, 0, p, sizeof(p));
    d.reverse = true; EXPECT_EQ(c.prepare(d, 99, p, sizeof(p)), 0);
    EXPECT_EQ(c.prepare(d, 100, p, sizeof(p)), 18);
    Engine::Channels decoded; EXPECT_TRUE(Engine::decode_channels(p, 18, decoded));
    EXPECT_EQ(decoded.mask, 0x41U); EXPECT_EQ(decoded.values[6], 60000);
    d.throttle = 0.4f; d.stopped = false; EXPECT_EQ(c.prepare(d, 101, p, sizeof(p)), 18);
    d.reverse = false; EXPECT_EQ(c.prepare(d, 300, p, sizeof(p)), 0);
    d.throttle = 0; d.stopped = true; EXPECT_EQ(c.prepare(d, 301, p, sizeof(p)), 0);
    EXPECT_EQ(c.prepare(d, 401, p, sizeof(p)), 18);
    Engine::decode_channels(p, 18, decoded); EXPECT_EQ(decoded.values[6], 2000);
}

TEST(SpektrumPhase4, ReverseDisabledInvalidProfileAndBuffer)
{
    uint8_t p[80]; auto d = ready(); Commands c(profile()); d.reverse = true;
    EXPECT_EQ(c.prepare(d, 0, p, sizeof(p)), 0);
    d.reverse = false; EXPECT_EQ(c.prepare(d, 0, p, 15), 0);
    EXPECT_EQ(c.prepare(d, 0, nullptr, sizeof(p)), 0);
    auto invalid = profile(true); invalid.reverse_channel = invalid.throttle_channel;
    Commands conflict(invalid); EXPECT_EQ(conflict.prepare(d, 0, p, sizeof(p)), 0);
    invalid = profile(); invalid.zero = 1001;
    Commands misaligned(invalid); EXPECT_EQ(misaligned.prepare(d, 0, p, sizeof(p)), 0);
}

TEST(SpektrumPhase4, RPMRequiresVerifiedPolesAndFreshness)
{
    Measurements m; Measurements::Calibration c; m.reset(c);
    Engine::Telemetry d {}; d.valid = Engine::ERPM; d.electrical_rpm = 70000;
    float result = 0; m.update(d, 0, 0, 0); EXPECT_FALSE(m.rpm(result, 0));
    c.poles_verified = true; c.motor_poles = 14; m.reset(c); m.update(d, 0, 0, 0);
    EXPECT_TRUE(m.rpm(result, 49)); EXPECT_NEAR(result, 10000.0f, 0.01f);
    EXPECT_FALSE(m.rpm(result, 50));
    c.motor_poles = 3; m.reset(c); m.update(d, 0, 0, 0); EXPECT_FALSE(m.rpm(result, 0));
}

TEST(SpektrumPhase4, CapacityRequiresKnownBaselineAndInputCurrent)
{
    Measurements m; Measurements::Calibration c; Engine::Telemetry d {};
    d.valid = Engine::CURRENT; d.current = 36;
    float result = 0; m.reset(c); m.update(d, 0, 0, 0); EXPECT_FALSE(m.capacity(result, 0));
    c.battery_input_current_verified = c.capacity_baseline_verified = true; c.baseline_mah = 10;
    m.reset(c); m.update(d, 0, 0, 1); m.update(d, 10, 10, 2);
    EXPECT_TRUE(m.capacity(result, 10)); EXPECT_NEAR(result, 10.1f, 0.001f);
    m.update(d, 10, 20, 2); EXPECT_TRUE(m.capacity(result, 20)); EXPECT_NEAR(result, 10.1f, 0.001f);
    m.update(d, 60, 60, 3); EXPECT_FALSE(m.capacity(result, 60));
    m.update(d, 70, 70, 4); EXPECT_FALSE(m.capacity(result, 70));
}

TEST(SpektrumPhase4, StaleMissingAndOutOfOrderMeasurements)
{
    Measurements m; Measurements::Calibration c;
    c.battery_input_current_verified = c.capacity_baseline_verified = true;
    Engine::Telemetry d {}; d.valid = Engine::CURRENT; d.current = 1; float result;
    m.reset(c); m.update(d, 100, 100, 1); m.update(d, 99, 101, 2);
    EXPECT_FALSE(m.capacity(result, 101));
    m.reset(c); m.update(d, 0, 50, 1); EXPECT_FALSE(m.capacity(result, 50));
    m.reset(c); d.current = NAN; m.update(d, 0, 0, 1); EXPECT_FALSE(m.capacity(result, 0));
    m.reset(c); d.valid = 0; m.update(d, 0, 0, 1); EXPECT_FALSE(m.capacity(result, 0));
}

TEST(SpektrumPhase5, DiagnosticHandshakeWhitelist)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_TRUE(Bus::diagnostic_packet(p.buffer, p.length));
    EXPECT_FALSE(Bus::diagnostic_packet(nullptr, 14));
    EXPECT_FALSE(Bus::diagnostic_packet(p.buffer, 13));
    uint8_t packet[14]; memcpy(packet, p.buffer, sizeof(packet));
    packet[3] = 0x20; seal(packet); EXPECT_FALSE(Bus::diagnostic_packet(packet, sizeof(packet)));
    packet[3] = 0x30; packet[6] = 1; seal(packet);
    EXPECT_FALSE(Bus::diagnostic_packet(packet, sizeof(packet)));
}

TEST(SpektrumPhase5, DiagnosticWhitelistRejectsMotorAndBadCRC)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_TRUE(b.request_telemetry(54000, true));
    EXPECT_TRUE(Bus::diagnostic_packet(p.buffer, p.length));
    uint8_t packet[14]; memcpy(packet, p.buffer, sizeof(packet));
    packet[3] = 1; seal(packet); EXPECT_FALSE(Bus::diagnostic_packet(packet, sizeof(packet)));
    packet[3] = 0; packet[8] = 1; seal(packet);
    EXPECT_FALSE(Bus::diagnostic_packet(packet, sizeof(packet)));
    packet[8] = 0; seal(packet); packet[13] ^= 1;
    EXPECT_FALSE(Bus::diagnostic_packet(packet, sizeof(packet)));
    Commands c(profile()); auto d = ready(); uint8_t motor[80];
    const uint8_t length = c.prepare(d, 0, motor, sizeof(motor));
    EXPECT_FALSE(Bus::diagnostic_packet(motor, length));
}

TEST(SpektrumPhase5, MockPollSchedulerGatesAndDefault)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    b.update(100000); EXPECT_EQ(p.sent, 2U);
    EXPECT_FALSE(b.set_mock_poll_interval(10000, false));
    EXPECT_FALSE(b.set_mock_poll_interval(9999, true));
    EXPECT_FALSE(b.set_mock_poll_interval(25001, true));
    p.real = true; EXPECT_FALSE(b.set_mock_poll_interval(10000, true));
    p.real = false; EXPECT_TRUE(b.set_mock_poll_interval(0, false));
}

static bool sample(Bus &b, uint32_t now)
{
    uint8_t t[] = {0xA6, 0x80, 22, 0x30, 0x20, 0, 0x04, 0xD2, 0x09, 0x60, 0x02, 0x8A,
                   0x04, 0xD2, 0x01, 0x90, 15, 120, 100, 150, 0, 0};
    seal(t); return b.observe(t, sizeof(t), now);
}

TEST(SpektrumPhase5, MockPollSchedulerCadence)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_TRUE(b.set_mock_poll_interval(10000, true));
    b.update(63399); EXPECT_EQ(p.sent, 2U);
    b.update(63400); EXPECT_EQ(p.sent, 3U); EXPECT_EQ(b.state(), State::TX_ACTIVE);
    p.done = true; b.update(64620); EXPECT_TRUE(sample(b, 65000));
    b.update(73399); EXPECT_EQ(p.sent, 3U);
    b.update(73400); EXPECT_EQ(p.sent, 4U);
    EXPECT_EQ(Engine::crc16(p.buffer, p.length), 0);
    EXPECT_TRUE(Bus::diagnostic_packet(p.buffer, p.length));
}

TEST(SpektrumPhase5, MockPollTimeoutDisablesScheduler)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_TRUE(b.set_mock_poll_interval(10000, true)); b.update(63400);
    p.done = true; b.update(64620); b.update(69620);
    EXPECT_EQ(b.state(), State::COMMUNICATION_LOST);
    EXPECT_FALSE(sample(b, 69621)); b.update(1000000); EXPECT_EQ(p.sent, 3U);
    connected(b, p); const unsigned sent = p.sent;
    b.update(80000); EXPECT_EQ(p.sent, sent); // Restart does not re-enable polling.
}

TEST(SpektrumPhase5, MockPollBusyBusNoBurst)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    EXPECT_TRUE(b.set_mock_poll_interval(10000, true)); p.high = false;
    b.update(63400); b.update(64000); EXPECT_EQ(p.sent, 2U);
    EXPECT_EQ(b.state(), State::HANDSHAKE_PENDING);
    p.high = true; b.update(90000); EXPECT_EQ(p.sent, 3U);
    p.done = true; b.update(91220); EXPECT_TRUE(sample(b, 92000));
    b.update(92001); EXPECT_EQ(p.sent, 3U);
    b.update(99999); EXPECT_EQ(p.sent, 3U);
    b.update(100000); EXPECT_EQ(p.sent, 4U);
}

TEST(SpektrumPhase5, UnexpectedPeerUIDIsFault)
{
    MockPort p; Bus b(p, verified_mock_gate()); connected(b, p);
    uint8_t changed[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 1, 4, 1, 2, 3, 4, 0, 0};
    seal(changed); EXPECT_FALSE(b.observe(changed, sizeof(changed), 54000));
    EXPECT_EQ(b.state(), State::FAULT); EXPECT_FALSE(p.driving);
    b.update(1000000); EXPECT_EQ(p.sent, 2U);
}

TEST(SpektrumPhase10, HandshakeBenchRequiresVolatileManualApproval)
{
    MockPort p; AP_SpektrumSmartESC_HandshakeBench bench(p, verified_mock_gate());
    bench.update(true, 50000); EXPECT_EQ(p.sent, 0U);
    EXPECT_FALSE(bench.authorize_once(false, true, 0, 1));
    EXPECT_FALSE(bench.authorize_once(true, false, 0, 1));
    p.real = true;
    EXPECT_FALSE(bench.authorize_once(true, true, 0, 1)); // Compile-time hardware gate remains zero.
    EXPECT_FALSE(p.driving); EXPECT_FALSE(p.owned); EXPECT_EQ(p.sent, 0U);
}

TEST(SpektrumPhase10, HandshakeBenchStopsAfterConfirmationWithoutPoll)
{
    MockPort p; AP_SpektrumSmartESC_HandshakeBench bench(p, verified_mock_gate());
    EXPECT_TRUE(bench.authorize_once(true, true, 0, 123));
    bench.update(true, 50000);
    EXPECT_EQ(p.buffer[1], 0x21); EXPECT_TRUE(Bus::diagnostic_packet(p.buffer, p.length));
    p.done = true; bench.update(true, 51220); EXPECT_FALSE(p.driving);
    uint8_t response[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 0, 4, 1, 2, 3, 4, 0, 0};
    seal(response); EXPECT_TRUE(bench.observe(response, sizeof(response), 52000));
    bench.update(true, 52200); EXPECT_EQ(p.sent, 2U); EXPECT_EQ(p.buffer[1], 0x21);
    EXPECT_TRUE(Bus::diagnostic_packet(p.buffer, p.length));
    p.done = true; bench.update(true, 53420);
    EXPECT_FALSE(bench.active()); EXPECT_FALSE(p.driving); EXPECT_FALSE(p.owned);
    bench.update(true, 1000000); EXPECT_EQ(p.sent, 2U);
}

TEST(SpektrumPhase10, HandshakeBenchDeadlineAndArmingRelease)
{
    for (unsigned condition = 0; condition < 2; condition++) {
        MockPort p; AP_SpektrumSmartESC_HandshakeBench bench(p, verified_mock_gate());
        const uint32_t start = 0xFFFF0000U;
        EXPECT_TRUE(bench.authorize_once(true, true, start, 1));
        bench.update(true, start + 50000); EXPECT_TRUE(p.driving);
        bench.update(condition == 0, start + (condition == 0 ? 200000 : 50001));
        EXPECT_FALSE(bench.active()); EXPECT_FALSE(p.driving); EXPECT_FALSE(p.owned);
        bench.update(true, start + 300000); EXPECT_EQ(p.sent, 1U);
    }
}

TEST(SpektrumPhase10, HandshakeBenchUnexpectedFrameReturnsToRX)
{
    MockPort p; AP_SpektrumSmartESC_HandshakeBench bench(p, verified_mock_gate());
    EXPECT_TRUE(bench.authorize_once(true, true, 0, 1));
    uint8_t frame[14] {}; frame[1] = 0xCD;
    EXPECT_FALSE(bench.observe(frame, sizeof(frame), 1));
    EXPECT_FALSE(bench.active()); EXPECT_FALSE(p.driving); EXPECT_FALSE(p.owned);
    bench.update(true, 50000); EXPECT_EQ(p.sent, 0U);
}

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    DefaultAndHardwareGateClosed(); DiscoveryTurnaroundAndOwnership(); PhysicalTCRequiredAndBufferStable();
    CollisionReleasesBus(); ReceiveDuringTransmitIsCollision(); StuckLowAndStuckTransmitter();
    MissingESCBoundedRetries(); BadCRCAndUnexpectedMaster(); RecoveryRequiresExplicitRestart();
    TelemetryPollFreshnessAndTiming(); ReleaseFailureAndExclusiveOwner(); ClockWrapAndUnexpectedDevice();
    CommandsRequireCalibrationAndNeutral(); ForwardMappingAndBounds(); DisarmRCLossESCLossAndFailsafe();
    ReverseRequiresStoppedDwell(); ReverseDisabledInvalidProfileAndBuffer(); RPMRequiresVerifiedPolesAndFreshness();
    CapacityRequiresKnownBaselineAndInputCurrent(); StaleMissingAndOutOfOrderMeasurements();
    DiagnosticHandshakeWhitelist(); DiagnosticWhitelistRejectsMotorAndBadCRC(); MockPollSchedulerGatesAndDefault();
    MockPollSchedulerCadence(); MockPollTimeoutDisablesScheduler(); MockPollBusyBusNoBurst(); UnexpectedPeerUIDIsFault();
    HandshakeBenchRequiresVolatileManualApproval(); HandshakeBenchStopsAfterConfirmationWithoutPoll();
    HandshakeBenchDeadlineAndArmingRelease(); HandshakeBenchUnexpectedFrameReturnsToRX();
    std::puts("31 mock transport/command/handshake-bench tests passed");
}
#else
AP_GTEST_MAIN()
#endif
