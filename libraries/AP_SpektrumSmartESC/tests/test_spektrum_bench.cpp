/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Bench.h"
#include "../AP_SpektrumSmartESC_Probe.h"
#ifdef SPKTSMART_STANDALONE_TEST
#include <cassert>
#include <cstdio>
#define TEST(group, name) static void name()
#define EXPECT_TRUE(v) assert(v)
#define EXPECT_FALSE(v) assert(!(v))
#define EXPECT_EQ(a, b) assert((a) == (b))
#else
#include "../AP_SpektrumSmartESC_config.h"
#include <AP_gtest.h>
#endif

TEST(SpektrumBench, DisabledAndRebootLatch)
{
    AP_SpektrumSmartESC_Bench b;
    EXPECT_FALSE(b.arming_blocked(0));
    EXPECT_TRUE(b.arming_blocked(1)); // Even before init/update.
    b.observe_enable(1);
    EXPECT_TRUE(b.arming_blocked(0));
    AP_SpektrumSmartESC_Bench rebooted;
    EXPECT_FALSE(rebooted.arming_blocked(0));
}

TEST(SpektrumBench, InvalidModesFailClosed)
{
    AP_SpektrumSmartESC_Bench b;
    EXPECT_TRUE(b.arming_blocked(-1));
    b.observe_enable(2);
    EXPECT_TRUE(b.arming_blocked(0));
}

TEST(SpektrumBench, NoTrafficTimeoutOnce)
{
    AP_SpektrumSmartESC_Bench b;
    b.update_silence(100, 0, 0, true);
    b.update_silence(149, 0, 0, true); EXPECT_EQ(b.timeouts(), 0U);
    b.update_silence(150, 0, 0, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(1000, 0, 0, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(1001, 1, 1001, true);
    b.update_silence(1051, 1, 1001, true); EXPECT_EQ(b.timeouts(), 2U);
}

TEST(SpektrumBench, AcquisitionAgeNotConsumptionTime)
{
    AP_SpektrumSmartESC_Bench first_batch;
    first_batch.update_silence(200, 10, 110, true);
    EXPECT_EQ(first_batch.timeouts(), 1U);
    AP_SpektrumSmartESC_Bench b;
    b.update_silence(100, 0, 0, true);
    b.update_silence(200, 10, 110, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(201, 10, 110, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(202, 11, 202, true);
    b.update_silence(251, 11, 202, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(252, 11, 202, true); EXPECT_EQ(b.timeouts(), 2U);
}

TEST(SpektrumBench, SilenceClockWrap)
{
    AP_SpektrumSmartESC_Bench b;
    b.update_silence(0xFFFFFFF0U, 0, 0, true);
    b.update_silence(0x21U, 0, 0, true); EXPECT_EQ(b.timeouts(), 0U);
    b.update_silence(0x22U, 0, 0, true); EXPECT_EQ(b.timeouts(), 1U);
}

TEST(SpektrumBench, InactiveReceiverDoesNotCountTimeouts)
{
    AP_SpektrumSmartESC_Bench b;
    b.update_silence(0, 0, 0, false);
    b.update_silence(1000, 0, 0, false); EXPECT_EQ(b.timeouts(), 0U);
    b.update_silence(1001, 0, 0, true);
    b.update_silence(1051, 0, 0, true); EXPECT_EQ(b.timeouts(), 1U);
    b.update_silence(2000, 0, 0, false);
    b.update_silence(3000, 0, 0, true); EXPECT_EQ(b.timeouts(), 1U);
}

TEST(SpektrumProbe, BootRequiresZeroThenExplicitRequest)
{
    AP_SpektrumSmartESC_Probe p;
    EXPECT_FALSE(p.request(2, 0, true, 0)); EXPECT_EQ(p.active(), 0);
    EXPECT_TRUE(p.request(0, 0, true, 1)); EXPECT_TRUE(p.request(2, 0, true, 2));
    EXPECT_EQ(p.active(), 2);
    AP_SpektrumSmartESC_Probe rebooted;
    EXPECT_FALSE(rebooted.request(2, 0, true, 3)); EXPECT_EQ(rebooted.active(), 0);
}
TEST(SpektrumProbe, InvalidArmedOrConflictingRequestCancels)
{
    AP_SpektrumSmartESC_Probe p;
    EXPECT_TRUE(p.request(0, 0, true, 0)); EXPECT_TRUE(p.request(1, 0, true, 1));
    EXPECT_FALSE(p.request(2, 0, false, 2)); EXPECT_EQ(p.active(), 0);
    EXPECT_FALSE(p.request(2, 0, true, 3));
    EXPECT_TRUE(p.request(0, 0, true, 4)); EXPECT_FALSE(p.request(2, 1, true, 5));
    EXPECT_TRUE(p.request(0, 0, true, 6)); EXPECT_FALSE(p.request(5, 0, true, 7));
    EXPECT_EQ(p.active(), 0);
}
TEST(SpektrumProbe, TimeoutWrapAndModeSwitchCannotExtend)
{
    AP_SpektrumSmartESC_Probe p; const uint32_t start = 0xFFFFF000U;
    EXPECT_TRUE(p.request(0, 0, true, start)); EXPECT_TRUE(p.request(1, 0, true, start));
    EXPECT_TRUE(p.request(3, 0, true, start + 59000));
    EXPECT_TRUE(p.request(4, 0, true, start + 59999));
    EXPECT_FALSE(p.request(2, 0, true, start + 60000)); EXPECT_EQ(p.active(), 0);
    EXPECT_FALSE(p.request(2, 0, true, start + 60001));
}
TEST(SpektrumProbe, ZeroEndsSessionAndRequiresNewRequest)
{
    AP_SpektrumSmartESC_Probe p;
    EXPECT_TRUE(p.request(0, 0, true, 0)); EXPECT_TRUE(p.request(4, 0, true, 1));
    EXPECT_TRUE(p.request(0, 0, true, 2)); EXPECT_EQ(p.active(), 0);
    EXPECT_TRUE(p.request(3, 0, true, 3)); EXPECT_EQ(p.active(), 3);
    p.cancel(); EXPECT_EQ(p.active(), 0); EXPECT_FALSE(p.request(3, 0, true, 4));
}

#if !defined(SPKTSMART_STANDALONE_TEST) && AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
#include <AP_Vehicle/AP_Vehicle.h>
#include <AP_Arming/AP_Arming.h>
#include <GCS_MAVLink/GCS.h>
#include <SRV_Channel/SRV_Channel.h>
#include "../AP_SpektrumSmartESC_Shadow.h"
#include <RC_Channel/RC_Channel.h>

const AP_HAL::HAL &hal = AP_HAL::get_HAL();
const AP_Param::Info vehicle_var_info[] = { AP_VAREND };
// This fixture has no receiver channels; status reporting only reads options.
const AP_Param::GroupInfo RC_Channels::var_info[] = { AP_GROUPEND };
AP_GTEST_PANIC()

class BenchGCS : public GCS {
public:
    uint32_t custom_mode() const override { return 0; }
    MAV_TYPE frame_type() const override { return MAV_TYPE_GENERIC; }
    GCS_MAVLINK *chan(uint8_t) override { return nullptr; }
    const GCS_MAVLINK *chan(uint8_t) const override { return nullptr; }
protected:
    GCS_MAVLINK *new_gcs_mavlink_backend(AP_HAL::UARTDriver &) override { return nullptr; }
};

class BenchRC : public RC_Channels {
public:
    RC_Channel *channel(uint8_t) override { return nullptr; }
    int8_t flight_mode_channel_number() const override { return 0; }
};

class BenchVehicle : public AP_Vehicle {
public:
    bool set_mode(uint8_t, ModeReason) override { return false; }
    uint8_t get_mode() const override { return 0; }
    void get_scheduler_tasks(const AP_Scheduler::Task *&tasks, uint8_t &count, uint32_t &log) override {
        tasks = nullptr; count = 0; log = 0;
    }
    AP_SpektrumSmartESC_Diagnostics &diagnostics() { return spektrum_smart_esc; }
protected:
    void init_ardupilot() override {}
    void load_parameters() override {}
};

class BenchArming : public AP_Arming {
public:
    void require_no() { require.set(Required::NO); }
    void already_armed(bool state) { armed = state; }
};

TEST(SpektrumBenchArming, ActualArmingEntryPointsAndNoArmRequired)
{
    // One vehicle/arming singleton in this executable. No scheduler or flight.
    static BenchVehicle vehicle;
    static BenchRC receiver_channels; // Status reporting consults RC options.
    static BenchGCS ground_station; // Zero channels: no serial or MAVLink output.
    static BenchArming arming;
    arming.require_no();
    EXPECT_EQ(arming.is_armed(), !bool(AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB)); // LAB veto also covers ARMING_REQUIRE=0.
    EXPECT_TRUE(AP_Param::set_object_value(&vehicle.diagnostics(), vehicle.diagnostics().var_info, "HS", 1));
    EXPECT_FALSE(arming.is_armed()); EXPECT_FALSE(arming.arm_force(AP_Arming::Method::MAVLINK));
    EXPECT_TRUE(AP_Param::set_object_value(&vehicle.diagnostics(), vehicle.diagnostics().var_info, "HS", 0));
    auto &diag = vehicle.diagnostics();
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "PROBE", 2));
    EXPECT_FALSE(arming.is_armed()); EXPECT_FALSE(arming.arm_force(AP_Arming::Method::MAVLINK));
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "HS", 1));
    diag.init(); // Stored nonzero request latches protection but cannot start probe.
    diag.update(); // Persisted HS=1 was discarded by init, not consumed as an attempt.
    EXPECT_EQ(diag.reader_status().state, AP_SpektrumSmartESC_Reader::State::DISABLED);
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "PROBE", 0));
    EXPECT_FALSE(arming.is_armed()); // Probe veto is latched until reboot.

    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "ENABLE", 1));
    EXPECT_FALSE(arming.is_armed());
    EXPECT_FALSE(arming.pre_arm_checks(false));
    EXPECT_FALSE(arming.arm(AP_Arming::Method::MAVLINK));
    EXPECT_FALSE(arming.arm_force(AP_Arming::Method::MAVLINK));
    hal.util->set_soft_armed(true);
    EXPECT_TRUE(arming.is_armed()); // Includes already-flying ARMING_REQUIRE=0.
    hal.util->set_soft_armed(false);
    arming.already_armed(true);
    EXPECT_TRUE(arming.is_armed()); // A live parameter edit must not auto-disarm.
    arming.already_armed(false);
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "ENABLE", 2));
    diag.init(); // Invalid mode latches the veto but starts no UART.
    EXPECT_TRUE(SRV_Channels::get_emergency_stop());
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "ENABLE", 0));
    EXPECT_FALSE(arming.arm_force(AP_Arming::Method::MAVLINK));
    EXPECT_FALSE(arming.is_armed());
    EXPECT_TRUE(AP_Param::set_object_value(&diag, diag.var_info, "HS", 1));
    diag.update();
    EXPECT_EQ(diag.reader_status().state, AP_SpektrumSmartESC_Reader::State::BLOCKED);
    EXPECT_EQ(diag.reader_status().tx_frames, 0U);
    EXPECT_EQ(diag.reader_status().rx_frames, 0U);
}
static SRV_Channels &bench_servo_outputs()
{
    static SRV_Channels outputs;
    return outputs;
}

TEST(SpektrumBenchShadow, ActualSRVCalculationOverridesAndEmergencyStop)
{
    auto &outputs = bench_servo_outputs();
    (void)outputs;
    EXPECT_TRUE(SRV_Channels::set_aux_channel_default(SRV_Channel::k_throttle, 2));
    auto *channel = SRV_Channels::srv_channel(2);
    ASSERT_NE(channel, nullptr);
    channel->set_output_min(1000); channel->set_output_max(2000);
    SRV_Channels::set_range(SRV_Channel::k_throttle, 100);
    const bool previous_stop = SRV_Channels::get_emergency_stop();
    SRV_Channels::set_emergency_stop(false);
    SRV_Channels::set_output_scaled(SRV_Channel::k_throttle, 75);
    SRV_Channels::calc_pwm();
    AP_SpektrumSmartESC_Control::Demand d;
    EXPECT_EQ(channel->get_output_pwm(), 1750);
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(channel->get_output_pwm(),
        1000, 1500, 2000, false, false, d));
    EXPECT_NEAR(d.throttle_percent, 75, 0.001f);
    // A forced PWM override supersedes the function's 75% scaled demand.
    SRV_Channels::set_output_pwm_chan(2, 1250);
    SRV_Channels::calc_pwm();
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(channel->get_output_pwm(),
        1000, 1500, 2000, false, false, d));
    EXPECT_NEAR(d.throttle_percent, 25, 0.001f);
    SRV_Channels::set_emergency_stop(true);
    SRV_Channels::set_output_scaled(SRV_Channel::k_throttle, 75);
    SRV_Channels::calc_pwm();
    EXPECT_EQ(channel->get_output_pwm(), 1000);
    EXPECT_TRUE(AP_SpektrumSmartESC_Shadow::plane_demand(channel->get_output_pwm(),
        1000, 1500, 2000, false, false, d));
    EXPECT_NEAR(d.throttle_percent, 0, 0.001f);
    SRV_Channels::set_output_pwm_chan(2, 0);
    EXPECT_FALSE(AP_SpektrumSmartESC_Shadow::plane_demand(channel->get_output_pwm(),
        1000, 1500, 2000, false, false, d));
    SRV_Channels::set_emergency_stop(previous_stop);
}

TEST(SpektrumProbe, ParameterVisibilityIndependentOfLegacyEnable)
{
    EXPECT_FALSE(AP_SpektrumSmartESC_Diagnostics::var_info[0].flags & AP_PARAM_FLAG_ENABLE);
}

TEST(SpektrumProbe, EmergencyStopDoesNotChangeAileronElevator)
{
    (void)bench_servo_outputs();
    EXPECT_TRUE(SRV_Channels::set_aux_channel_default(SRV_Channel::k_aileron, 0));
    EXPECT_TRUE(SRV_Channels::set_aux_channel_default(SRV_Channel::k_elevator, 1));
    const bool old_stop = SRV_Channels::get_emergency_stop();
    SRV_Channels::set_emergency_stop(false);
    SRV_Channels::set_output_scaled(SRV_Channel::k_aileron, 1000);
    SRV_Channels::set_output_scaled(SRV_Channel::k_elevator, -500);
    SRV_Channels::calc_pwm();
    const uint16_t aileron = SRV_Channels::srv_channel(0)->get_output_pwm();
    const uint16_t elevator = SRV_Channels::srv_channel(1)->get_output_pwm();
    SRV_Channels::set_emergency_stop(true);
    SRV_Channels::set_output_scaled(SRV_Channel::k_aileron, 1000);
    SRV_Channels::set_output_scaled(SRV_Channel::k_elevator, -500);
    SRV_Channels::calc_pwm();
    EXPECT_EQ(SRV_Channels::srv_channel(0)->get_output_pwm(), aileron);
    EXPECT_EQ(SRV_Channels::srv_channel(1)->get_output_pwm(), elevator);
    SRV_Channels::set_emergency_stop(old_stop);
}

#endif

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    DisabledAndRebootLatch(); InvalidModesFailClosed(); NoTrafficTimeoutOnce();
    AcquisitionAgeNotConsumptionTime(); SilenceClockWrap(); InactiveReceiverDoesNotCountTimeouts();
    BootRequiresZeroThenExplicitRequest(); InvalidArmedOrConflictingRequestCancels();
    TimeoutWrapAndModeSwitchCannotExtend(); ZeroEndsSessionAndRequiresNewRequest();
    std::puts("10 bench/probe policy tests passed");
}
#else
AP_GTEST_MAIN()
#endif
