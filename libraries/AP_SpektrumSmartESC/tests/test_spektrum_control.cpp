/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Control.h"
#include "../AP_SpektrumSmartESC_Shadow.h"
#include "../AP_SpektrumSmartESC_Bus.h"
#include <cmath>
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
#endif

using Control = AP_SpektrumSmartESC_Control;
using Engine = AP_SpektrumSmartESC;
using Transport = Control::Transport;

static Control::Calibration synthetic_calibration(bool reverse_verified = true)
{
    Control::Calibration c;
    // Fixture only. These are NOT claimed Avian endpoints or reverse levels.
    c.channels.throttle_verified = true;
    c.channels.throttle_channel = 2; // Caller numbering cannot override the fixed SRXL2 mapping.
    c.channels.reverse_channel = 10;
    c.channels.zero = 1000; c.channels.full = 61000;
    c.channels.reverse_verified = true;
    c.channels.forward = 2000; c.channels.reverse = 60000;
    c.channels.direction_dwell_ms = 100;
    c.reverse_settings_verified = reverse_verified;
    return c;
}
static Control::Qualification mock_qualification()
{
    Control::Qualification q; q.electrical_verified = q.protocol_verified = true; return q;
}
static Control::Ownership mock_ownership(const Control &c, uint32_t now)
{
    Control::Ownership o; o.epoch = c.epoch(); o.acquired_ms = now;
    o.main3_pwm_inhibited = o.aux7_pwm_reserved = o.exclusive = true;
    return o;
}
static Control::Demand ready()
{
    Control::Demand d; d.armed = d.rc_ok = d.esc_ok = d.motor_stopped = true; return d;
}
static uint8_t prepare(Control &c, const Control::Demand &d, uint32_t now, uint8_t *p)
{
    return c.prepare_simulated(d, mock_ownership(c, now), now, p, 80);
}
static void start(Control &c, uint8_t *p, uint32_t now = 0)
{
    EXPECT_TRUE(c.select_simulated(Transport::Aux7UART4, mock_qualification(), true));
    EXPECT_TRUE(prepare(c, ready(), now, p) != 0);
}
static Engine::Channels decode(const uint8_t *p, uint8_t n)
{
    Engine::Channels result {};
    EXPECT_TRUE(Engine::decode_channels(p, n, result));
    return result;
}

TEST(SpektrumControl, DefaultAndUnsupportedTransport)
{
    Control c(synthetic_calibration()); uint8_t p[80]; Control::Qualification unknown;
    EXPECT_EQ(c.transport(), Transport::Disabled);
    EXPECT_EQ(prepare(c, ready(), 0, p), 0);
    EXPECT_FALSE(c.select_simulated(Transport::Aux7UART4, unknown, true));
    EXPECT_FALSE(c.select_simulated(Transport::Main3IOMCU, mock_qualification(), true));
    EXPECT_FALSE(c.select_simulated(Transport::Aux7UART4, mock_qualification(), false));
    EXPECT_EQ(c.transport(), Transport::Disabled);
}

TEST(SpektrumControl, FixedChannelDomains)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    const auto data = decode(p, 18);
    EXPECT_EQ(data.mask, 0x41U);
    EXPECT_EQ(data.values[0], 1000); EXPECT_EQ(data.values[6], 2000);
    EXPECT_EQ(data.values[2], 0); EXPECT_EQ(data.values[10], 0);
    EXPECT_EQ(data.reply_id, 0); EXPECT_FALSE(data.failsafe);
}

TEST(SpektrumControl, ForwardControllerMagnitude)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
    d.throttle_percent = 50;
    EXPECT_EQ(prepare(c, d, 1, p), 18); EXPECT_EQ(decode(p, 18).values[0], 31000);
    d.throttle_percent = 100;
    EXPECT_EQ(prepare(c, d, 2, p), 18); EXPECT_EQ(decode(p, 18).values[0], 61000);
}

TEST(SpektrumControl, InitialNeutralRequired)
{
    Control c(synthetic_calibration()); uint8_t p[80]; auto d = ready();
    EXPECT_TRUE(c.select_simulated(Transport::Aux7UART4, mock_qualification(), true));
    d.throttle_percent = 20; EXPECT_EQ(prepare(c, d, 0, p), 0);
    d.throttle_percent = 0; d.motor_stopped = false; EXPECT_EQ(prepare(c, d, 1, p), 0);
    d.motor_stopped = true; EXPECT_EQ(prepare(c, d, 2, p), 18);
}

TEST(SpektrumControl, NegativeDemandStopsBeforeDirectionChange)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
    d.throttle_percent = 50; prepare(c, d, 1, p);
    d.throttle_percent = -40; d.reverse_permitted = true; d.motor_stopped = false;
    EXPECT_EQ(prepare(c, d, 10, p), 18);
    EXPECT_EQ(decode(p, 18).values[0], 1000); EXPECT_EQ(decode(p, 18).values[6], 2000);
    d.motor_stopped = true;
    prepare(c, d, 20, p); prepare(c, d, 119, p);
    EXPECT_EQ(decode(p, 18).values[6], 2000);
    prepare(c, d, 120, p);
    EXPECT_EQ(decode(p, 18).values[0], 1000); EXPECT_EQ(decode(p, 18).values[6], 60000);
    d.motor_stopped = false; prepare(c, d, 121, p);
    EXPECT_EQ(decode(p, 18).values[0], 25000);
}

TEST(SpektrumControl, ExplicitZeroMagnitudeReverseAndReturn)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
    d.reverse_request = d.reverse_permitted = true; prepare(c, d, 100, p);
    EXPECT_EQ(decode(p, 18).values[6], 60000);
    d.reverse_request = false; prepare(c, d, 110, p);
    EXPECT_EQ(decode(p, 18).values[6], 60000);
    prepare(c, d, 200, p); EXPECT_EQ(decode(p, 18).values[6], 2000);
}

TEST(SpektrumControl, ReverseSettingsAndVehiclePermissionRequired)
{
    uint8_t p[80]; auto d = ready();
    Control unverified(synthetic_calibration(false)); start(unverified, p);
    EXPECT_EQ(decode(p, 16).mask, 1U);
    d.throttle_percent = -1; d.reverse_permitted = true;
    EXPECT_EQ(prepare(unverified, d, 1, p), 0);
    Control c(synthetic_calibration()); start(c, p); d.reverse_permitted = false;
    EXPECT_EQ(prepare(c, d, 1, p), 0); EXPECT_EQ(c.transport(), Transport::Disabled);
}

TEST(SpektrumControl, DuplicateOutputAndStaleOwnershipRejected)
{
    for (unsigned fault = 0; fault < 6; fault++) {
        Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
        auto o = mock_ownership(c, 100);
        if (fault == 0) { o.main3_pwm_inhibited = false; }
        if (fault == 1) { o.aux7_pwm_reserved = false; }
        if (fault == 2) { o.exclusive = false; }
        if (fault == 3) { o.epoch++; }
        if (fault == 4) { o.acquired_ms = 50; }
        if (fault == 5) { o.acquired_ms = 101; }
        EXPECT_EQ(c.prepare_simulated(ready(), o, 100, p, sizeof(p)), 0);
        EXPECT_EQ(c.transport(), Transport::Disabled);
        EXPECT_EQ(prepare(c, ready(), 101, p), 0); // Never restores legacy PWM or digital automatically.
    }
}

TEST(SpektrumControl, CommunicationLossAndFailsafeRequireReselect)
{
    for (unsigned fault = 0; fault < 3; fault++) {
        Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
        if (fault == 0) { d.esc_ok = false; }
        if (fault == 1) { d.rc_ok = false; }
        if (fault == 2) { d.failsafe = true; }
        EXPECT_EQ(prepare(c, d, 1, p), 0); EXPECT_EQ(prepare(c, ready(), 2, p), 0);
        EXPECT_TRUE(c.select_simulated(Transport::Aux7UART4, mock_qualification(), true));
        d = ready(); d.throttle_percent = 10;
        EXPECT_EQ(prepare(c, d, 3, p), 0);
        d.throttle_percent = 0; EXPECT_EQ(prepare(c, d, 4, p), 18);
    }
}

TEST(SpektrumControl, DisarmAndSuppressionResetAuthorization)
{
    for (unsigned suppress = 0; suppress < 2; suppress++) {
        Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
        d.throttle_percent = 40; prepare(c, d, 1, p);
        d.armed = suppress != 0; d.throttle_suppressed = suppress != 0;
        EXPECT_EQ(prepare(c, d, 2, p), 0);
        d = ready(); d.throttle_percent = 40; EXPECT_EQ(prepare(c, d, 3, p), 0);
        d.throttle_percent = 0; EXPECT_EQ(prepare(c, d, 4, p), 18);
    }
}

TEST(SpektrumControl, InvalidInputAndSmallBuffers)
{
    const float invalid[] = {NAN, INFINITY, -INFINITY, 101, -101};
    for (float value : invalid) {
        Control c(synthetic_calibration()); uint8_t p[80]; start(c, p); auto d = ready();
        d.throttle_percent = value; EXPECT_EQ(prepare(c, d, 1, p), 0);
        EXPECT_EQ(c.transport(), Transport::Disabled);
    }
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    EXPECT_EQ(c.prepare_simulated(ready(), mock_ownership(c, 1), 1, p, 17), 0);
    EXPECT_EQ(c.prepare_simulated(ready(), mock_ownership(c, 1), 1, nullptr, 80), 0);
}

TEST(SpektrumControl, NativeDiagnosticPortRejectsControlFrames)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    EXPECT_FALSE(AP_SpektrumSmartESC_Bus::diagnostic_packet(p, 18));
}

TEST(SpektrumControl, OwnershipClockWrapAndEpoch)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p, 0xFFFFFFF0U);
    const auto old = mock_ownership(c, 0xFFFFFFF0U);
    EXPECT_EQ(c.prepare_simulated(ready(), old, 0x21U, p, sizeof(p)), 18);
    EXPECT_EQ(c.prepare_simulated(ready(), old, 0x22U, p, sizeof(p)), 0);
    EXPECT_TRUE(c.select_simulated(Transport::Aux7UART4, mock_qualification(), true));
    EXPECT_EQ(c.prepare_simulated(ready(), old, 0x23U, p, sizeof(p)), 0);
}

// Boundary fixtures, not a live SRV_Channel/RC_Channel adapter. Only the final
// logical throttle demand and an explicitly configured reverse source are copied.
struct ChannelIsolationFixture {
    float servo[8] = {};
    float rc[8] = {};
    bool reverse_source_configured = false;
    Control::Demand demand() const
    {
        auto d = ready();
        d.throttle_percent = servo[2];
        d.reverse_request = reverse_source_configured && rc[6] > 0;
        return d;
    }
};

TEST(SpektrumControl, ServoAileronAndRCInputsAreNotThrottleSources)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    ChannelIsolationFixture inputs;
    inputs.servo[2] = 25;
    for (float position : {-100.0f, 0.0f, 100.0f}) {
        inputs.servo[0] = position;
        inputs.rc[0] = position;
        inputs.rc[6] = position; // No configured reverse source.
        EXPECT_EQ(prepare(c, inputs.demand(), 1, p), 18);
        const auto data = decode(p, 18);
        EXPECT_EQ(data.mask, 0x41U);
        EXPECT_EQ(data.values[0], 16000);
        EXPECT_EQ(data.values[6], 2000);
    }
    inputs.servo[2] = 75;
    EXPECT_EQ(prepare(c, inputs.demand(), 2, p), 18);
    EXPECT_EQ(decode(p, 18).values[0], 46000);
}

TEST(SpektrumControl, ConfiguredReverseCannotWriteFlightSurfaces)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    ChannelIsolationFixture inputs;
    inputs.servo[0] = 12; inputs.servo[1] = -34; inputs.servo[3] = 56;
    inputs.reverse_source_configured = true;
    inputs.rc[6] = 100;
    auto d = inputs.demand();
    EXPECT_EQ(prepare(c, d, 100, p), 0); // Configuration alone grants no permission.
    start(c, p);
    d.reverse_permitted = true;
    EXPECT_EQ(prepare(c, d, 100, p), 18);
    const auto data = decode(p, 18);
    EXPECT_EQ(data.mask, 0x41U); // No surface channels are transmitted.
    EXPECT_EQ(data.values[0], 1000);
    EXPECT_EQ(data.values[6], 60000);
    EXPECT_TRUE(std::fabs(inputs.servo[0] - 12) < 0.001f);
    EXPECT_TRUE(std::fabs(inputs.servo[1] + 34) < 0.001f);
    EXPECT_TRUE(std::fabs(inputs.servo[3] - 56) < 0.001f);
}

TEST(SpektrumControl, ActiveDemandNeverBypassesMain3Inhibit)
{
    Control c(synthetic_calibration()); uint8_t p[80]; start(c, p);
    auto d = ready(); d.throttle_percent = 80;
    EXPECT_EQ(prepare(c, d, 1, p), 18);
    auto owner = mock_ownership(c, 2);
    owner.main3_pwm_inhibited = false;
    EXPECT_EQ(c.prepare_simulated(d, owner, 2, p, sizeof(p)), 0);
    EXPECT_EQ(c.transport(), Transport::Disabled);
    EXPECT_EQ(prepare(c, d, 3, p), 0); // Even a new mock ACK cannot restart it.
}

TEST(SpektrumControl, ShadowFinalPWMRangeAndReversal)
{
    Control::Demand d;
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(1250, 1000, 1500, 2000, false, false, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent - 25) < 0.001f);
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(1750, 1000, 1500, 2000, true, false, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent - 25) < 0.001f);
    EXPECT_FALSE(d.esc_ok); EXPECT_FALSE(d.motor_stopped); EXPECT_FALSE(d.armed);
}

TEST(SpektrumControl, ShadowBipolarAsymmetricTrim)
{
    Control::Demand d;
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(1200, 1000, 1400, 2000, false, true, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent + 50) < 0.001f); EXPECT_TRUE(d.reverse_request);
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(1700, 1000, 1400, 2000, true, true, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent + 50) < 0.001f);
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(1400, 1000, 1400, 2000, false, true, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent) < 0.001f); EXPECT_FALSE(d.reverse_request);
}

TEST(SpektrumControl, ShadowInvalidPWMDoesNotReuseDemand)
{
    Control::Demand d;
    d.throttle_percent = 80; d.reverse_request = true;
    EXPECT_FALSE(AP_SpektrumSmartESC_Shadow::plane_demand(0, 1000, 1500, 2000, false, false, d));
    EXPECT_TRUE(std::fabs(d.throttle_percent) < 0.001f); EXPECT_FALSE(d.reverse_request);
    EXPECT_FALSE(AP_SpektrumSmartESC_Shadow::plane_demand(2200, 1000, 1500, 2000, false, false, d));
    EXPECT_FALSE(AP_SpektrumSmartESC_Shadow::plane_demand(1500, 2000, 1500, 1000, false, false, d));
    EXPECT_FALSE(AP_SpektrumSmartESC_Shadow::plane_demand(1500, 1000, 1000, 2000, false, true, d));
}

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    DefaultAndUnsupportedTransport(); FixedChannelDomains(); ForwardControllerMagnitude(); InitialNeutralRequired();
    NegativeDemandStopsBeforeDirectionChange(); ExplicitZeroMagnitudeReverseAndReturn();
    ReverseSettingsAndVehiclePermissionRequired(); DuplicateOutputAndStaleOwnershipRejected();
    CommunicationLossAndFailsafeRequireReselect(); DisarmAndSuppressionResetAuthorization();
    InvalidInputAndSmallBuffers(); NativeDiagnosticPortRejectsControlFrames(); OwnershipClockWrapAndEpoch();
    ServoAileronAndRCInputsAreNotThrottleSources(); ConfiguredReverseCannotWriteFlightSurfaces();
    ActiveDemandNeverBypassesMain3Inhibit();
    ShadowFinalPWMRangeAndReversal(); ShadowBipolarAsymmetricTrim(); ShadowInvalidPWMDoesNotReuseDemand();
    std::puts("19 simulated control/isolation/shadow tests passed");
}
#else
AP_GTEST_MAIN()
#endif
