/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_gtest.h>
#include <AP_HAL/AP_HAL.h>
#include "../AP_SpektrumSmartESC_Diagnostics.h"
#include "../AP_SpektrumSmartESC_Lab.h"
#include "../AP_SpektrumSmartESC_Plane.h"
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#include <vector>
static AP_HAL::UARTDriver *hs_test_port;
class HandshakeTestHAL : public AP_HAL::HAL {
public:
    HandshakeTestHAL() : AP_HAL::HAL(AP_HAL::get_HAL()) {}
    AP_HAL::UARTDriver *diagnostic_rx_uart() const override { return hs_test_port; }
    void run(int, char *const[], Callbacks *) const override {}
};
static HandshakeTestHAL hs_test_hal;
const AP_HAL::HAL &hal = hs_test_hal;
#else
const AP_HAL::HAL &hal = AP_HAL::get_HAL();
#endif
#if AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
#include <SRV_Channel/SRV_Channel.h>
#include <GCS_MAVLink/GCS.h>
#include <AP_Vehicle/AP_Vehicle.h>
#include <RC_Channel/RC_Channel.h>
const AP_Param::Info vehicle_var_info[] = { AP_VAREND };
const AP_Param::GroupInfo RC_Channels::var_info[] = { AP_GROUPEND };
class LabVehicle : public AP_Vehicle {
public:
    bool set_mode(uint8_t, ModeReason) override { return false; }
    uint8_t get_mode() const override { return 0; }
    void get_scheduler_tasks(const AP_Scheduler::Task *&tasks, uint8_t &count, uint32_t &log) override {
        tasks=nullptr; count=0; log=0;
    }
    AP_SpektrumSmartESC_Diagnostics &diagnostics() { return spektrum_smart_esc; }
protected:
    void init_ardupilot() override {}
    void load_parameters() override {}
};
class LabRC : public RC_Channels {
public:
    RC_Channel *channel(uint8_t) override { return nullptr; }
    int8_t flight_mode_channel_number() const override { return 0; }
};
class LabGCS : public GCS {
public:
    uint32_t custom_mode() const override { return 0; }
    MAV_TYPE frame_type() const override { return MAV_TYPE_GENERIC; }
    GCS_MAVLINK *chan(uint8_t) override { return nullptr; }
    const GCS_MAVLINK *chan(uint8_t) const override { return nullptr; }
protected:
    GCS_MAVLINK *new_gcs_mavlink_backend(AP_HAL::UARTDriver &) override { return nullptr; }
};
static LabVehicle &lab_context()
{
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    // The SITL HAL is constructed in another translation unit. Refresh its
    // services after static initialization, before constructing the fixtures.
    static const bool services_ready = []() {
        static_cast<AP_HAL::HAL &>(hs_test_hal) = AP_HAL::get_HAL();
        return true;
    }();
    (void)services_ready;
#endif
    static LabGCS ground_station;
    (void)ground_station;
    static LabVehicle vehicle;
    static LabRC receiver;
    (void)receiver;
    static SRV_Channels outputs;
    (void)outputs;
    return vehicle;
}
TEST(SpektrumLab, StartupNeverRunsSavedWaveformAndRequestsAreConsumed)
{
    auto &vehicle=lab_context();
    auto &diagnostic=vehicle.diagnostics();
    const bool previous_stop = SRV_Channels::get_emergency_stop();
    EXPECT_TRUE(AP_Param::set_object_value(&diagnostic, diagnostic.var_info,"WAVE",1));
    diagnostic.init();
    EXPECT_FALSE(diagnostic.waveform_requested());
    EXPECT_TRUE(AP_Param::set_object_value(&diagnostic, diagnostic.var_info,"WAVE",1));
    diagnostic.update();
    EXPECT_FALSE(diagnostic.waveform_requested());
    EXPECT_EQ(diagnostic.arming_blocked(),bool(AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB));
    if (AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) { EXPECT_TRUE(SRV_Channels::get_emergency_stop()); }
    SRV_Channels::set_emergency_stop(previous_stop);
}
TEST(SpektrumLab, RealSRVCalculationFeedsFinalThrottleOnly)
{
    lab_context();
    auto *throttle=SRV_Channels::srv_channel(2);
    ASSERT_NE(throttle,nullptr);
    ASSERT_TRUE(AP_Param::set_object_value(throttle,SRV_Channel::var_info,"FUNCTION",70));
    ASSERT_TRUE(AP_Param::set_object_value(throttle,SRV_Channel::var_info,"TRIM",1500));
    throttle->set_output_min(1000); throttle->set_output_max(2000);
    const uint8_t indexes[] = {0, 1, 3};
    const uint16_t functions[] = {4, 19, 21};
    for (uint8_t i=0;i<3;i++) {
        auto *surface=SRV_Channels::srv_channel(indexes[i]);
        ASSERT_TRUE(AP_Param::set_object_value(surface,SRV_Channel::var_info,"FUNCTION",functions[i]));
        ASSERT_TRUE(AP_Param::set_object_value(surface,SRV_Channel::var_info,"TRIM",1500));
        ASSERT_TRUE(AP_Param::set_object_value(surface,SRV_Channel::var_info,"REVERSED",0));
        surface->set_output_min(1000); surface->set_output_max(2000); surface->set_angle(4500);
    }
    SRV_Channels::set_emergency_stop(false);
    SRV_Channels::update_aux_servo_function();
    AP_SpektrumSmartESC_Plane::Snapshot s;
    s.servo_index=2; s.function=70; s.minimum=1000; s.trim=1500; s.maximum=2000;
    s.armed=s.rc_ok=s.control_ok=true;
    for (unsigned bipolar=0;bipolar<=1;bipolar++) {
        for (unsigned reversed=0;reversed<=1;reversed++) {
            ASSERT_TRUE(AP_Param::set_object_value(throttle,SRV_Channel::var_info,"REVERSED",reversed));
            if (bipolar) { throttle->set_angle(100); } else { throttle->set_range(100); }
            s.bipolar=bipolar; s.reversed=reversed;
            for (unsigned quarter=0;quarter<=4;quarter++) {
                // Same function API used by MANUAL and automatic controllers.
                SRV_Channels::set_output_scaled(SRV_Channel::k_throttle,quarter*25);
                SRV_Channels::set_output_scaled(SRV_Channel::k_aileron,4500-quarter*900);
                SRV_Channels::set_output_scaled(SRV_Channel::k_elevator,-4500+int(quarter)*900);
                SRV_Channels::set_output_scaled(SRV_Channel::k_rudder,quarter*900);
                SRV_Channels::calc_pwm();
                s.pwm=throttle->get_output_pwm();
                AP_SpektrumSmartESC_Motor::Demand d;
                ASSERT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,1,d));
                EXPECT_NEAR(d.magnitude,quarter*0.25f,0.0001f);
                EXPECT_FALSE(d.reverse);
                EXPECT_EQ(SRV_Channels::srv_channel(0)->get_output_pwm(),2000-quarter*100);
                EXPECT_EQ(SRV_Channels::srv_channel(1)->get_output_pwm(),1000+quarter*100);
                EXPECT_EQ(SRV_Channels::srv_channel(3)->get_output_pwm(),1500+quarter*100);
            }
        }
    }
    ASSERT_TRUE(AP_Param::set_object_value(throttle,SRV_Channel::var_info,"REVERSED",0));
    throttle->set_range(100);
    SRV_Channels::set_output_scaled(SRV_Channel::k_throttle,125);
    SRV_Channels::calc_pwm();
    EXPECT_EQ(throttle->get_output_pwm(),2000);
    SRV_Channels::set_output_scaled(SRV_Channel::k_throttle,-25);
    SRV_Channels::calc_pwm();
    EXPECT_EQ(throttle->get_output_pwm(),1000);
    SRV_Channels::set_emergency_stop(true);
    SRV_Channels::set_output_scaled(SRV_Channel::k_throttle,100);
    SRV_Channels::calc_pwm();
    EXPECT_EQ(throttle->get_output_pwm(),1000);
    SRV_Channels::set_emergency_stop(false);
}

TEST(SpektrumLab, FailedDigitalSelectionCannotFallBackToLegacyArming)
{
    AP_SpektrumSmartESC_Diagnostics attempted;
    EXPECT_FALSE(attempted.prepare_motor({}, {}, 0x40, 1, 1));
    EXPECT_TRUE(attempted.arming_blocked());
    EXPECT_TRUE(AP_Param::set_object_value(&attempted, attempted.var_info,"ENABLE",0));
    EXPECT_TRUE(attempted.arming_blocked());
}
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
class HandshakeTestPort : public AP_HAL::UARTDriver {
public:
    HandshakeTestPort() { lock_write_key = lock_read_key = 0; }
    bool initialized = false, owned = false, done = false, idle = true;
    uint32_t tc = 0;
    unsigned starts = 0, finishes = 0;
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> incoming;
    size_t offset = 0;
    AP_SpektrumSmartESC_LabHandshake::Lease lease;
    bool is_receive_only() const override { return !owned; }
    bool is_initialized() override { return initialized; }
    bool tx_pending() override { return false; }
    uint32_t txspace() override { return 0; }
    bool receive_diagnostics(ReceiveDiagnostics &s) const override {
        s = {}; s.flags = initialized ? 11 : 0; s.cr1 = initialized ? 5 : 0; s.cr3 = 8;
        s.handshake_flags = 16 | (owned ? 2 : 0) | (done ? 4 : 0) | (idle ? 32 : 0); s.handshake_tc_ms = tc;
        return true;
    }
    bool diagnostic_handshake_start(uint8_t target, uint32_t uid, bool power_on = false) override {
        starts++; return lease.start(target, uid, AP_HAL::millis(), power_on);
    }
    bool diagnostic_handshake(const uint8_t *p, uint8_t n, bool finish) override {
        if (finish) { finishes++; owned = done = false; lease.close(); return false; }
        if (p != nullptr) {
            if (!lease.take(p, n, AP_HAL::millis())) { return false; }
            sent.emplace_back(p, p + n); owned = true; done = false;
        }
        return owned && lease.active(AP_HAL::millis());
    }
    bool read_timed(uint8_t &byte, uint32_t &ms) override {
        if (offset >= incoming.size()) { return false; }
        byte = incoming[offset++]; ms = AP_HAL::millis(); return true;
    }
protected:
    void _begin(uint32_t baud, uint16_t, uint16_t) override { initialized = baud == 115200; }
    void _end() override { initialized = false; }
    void _flush() override {}
    size_t _write(const uint8_t *, size_t) override { ADD_FAILURE(); return 0; }
    ssize_t _read(uint8_t *, uint16_t) override { return 0; }
    uint32_t _available() override { return incoming.size() - offset; }
    bool _discard_input() override { offset = incoming.size(); return true; }
};
static void hs_time(uint32_t ms) { hal.scheduler->stop_clock(uint64_t(ms) * 1000U); }
static void hs_set(AP_SpektrumSmartESC_Diagnostics &d, const char *name, float value) {
    ASSERT_TRUE(AP_Param::set_object_value(&d, d.var_info, name, value));
}
TEST(SpektrumLab, RealDiagnosticsManualBoundedHandshakeWithMockPort)
{
    lab_context(); hs_time(1000);
    HandshakeTestPort port; hs_test_port = &port;
    AP_SpektrumSmartESC_Diagnostics d;
    hs_set(d, "ENABLE", 1); hs_set(d, "LABACK", 15); hs_set(d, "HS", 1);
    d.init(); d.update(); // Saved authorization and trigger must not execute.
    EXPECT_TRUE(d.arming_blocked()); EXPECT_TRUE(SRV_Channels::get_emergency_stop());
    EXPECT_EQ(port.starts, 0U); EXPECT_TRUE(port.sent.empty());
    hs_set(d, "LABACK", 15); d.update();
    hs_set(d, "HS", 1); d.update(); EXPECT_EQ(port.starts, 1U);
    hs_time(1050); d.update(); ASSERT_EQ(port.sent.size(), 1U);
    EXPECT_EQ(port.sent[0][1], 0x21); EXPECT_EQ(port.sent[0][4], 0x40);
    hs_time(1052); port.done = true; port.tc = AP_HAL::millis();
    port.incoming = port.sent[0]; // An echo cannot authorize confirmation.
    d.update(); EXPECT_EQ(d.reader_status().echoes, 1U); EXPECT_EQ(d.reader_status().responses, 0);
    uint8_t response[14]; const uint8_t payload[] = {0x40, 0x31, 10, 0, 0, 1, 2, 3, 4};
    ASSERT_EQ(AP_SpektrumSmartESC::encode(0x21, payload, sizeof(payload), response, sizeof(response)), 14);
    port.incoming.insert(port.incoming.end(), response, response + sizeof(response));
    hs_time(1054); d.update(); EXPECT_EQ(d.reader_status().responses, 1);
    hs_time(1060); d.update(); ASSERT_EQ(port.sent.size(), 2U);
    EXPECT_EQ(port.sent[1][1], 0x21); EXPECT_EQ(port.sent[1][4], 0xFF);
    hs_time(1062); port.done = true; port.tc = AP_HAL::millis(); d.update();
    EXPECT_EQ(d.reader_status().state, AP_SpektrumSmartESC_Reader::State::DISCOVERED);
    EXPECT_EQ(d.reader_status().tx_frames, 2U); EXPECT_FALSE(port.owned); EXPECT_TRUE(port.initialized);
    EXPECT_EQ(port.finishes, 1U);
    hs_time(7000); hs_set(d, "LABACK", 15); d.update(); hs_set(d, "HS", 1); d.update();
    EXPECT_EQ(port.starts, 1U); EXPECT_EQ(port.sent.size(), 2U); // No second experiment this boot.
    hs_test_port = nullptr;
}
TEST(SpektrumLab, RealDiagnosticsAbortAndTimeoutRestoreMockRX)
{
    lab_context();
    for (uint8_t abort = 0; abort < 3; abort++) {
        hs_time(10000 + abort * 1000);
        const uint32_t start = AP_HAL::millis();
        HandshakeTestPort port; hs_test_port = &port;
        AP_SpektrumSmartESC_Diagnostics d; hs_set(d, "ENABLE", 1); d.init();
        hs_set(d, "LABACK", 15); d.update(); hs_set(d, "HS", 1); d.update();
        hs_time(start + 50); d.update(); ASSERT_EQ(port.sent.size(), 1U);
        hs_time(start + 52); port.done = true; port.tc = AP_HAL::millis(); d.update();
        if (abort == 0) { hs_time(start + 103); }
        if (abort == 1) { hs_set(d, "HS", -1); }
        if (abort == 2) { hs_set(d, "ENABLE", 0); }
        d.update(); EXPECT_FALSE(port.owned); EXPECT_TRUE(port.initialized); EXPECT_EQ(port.finishes, 1U);
        EXPECT_EQ(port.sent.size(), 1U); EXPECT_EQ(d.reader_status().responses, 0);
        EXPECT_TRUE(d.arming_blocked());
    }
    hs_test_port = nullptr;
}
TEST(SpektrumLab, PowerOnWindowActualFrontendAndAbortPrepared)
{
    lab_context();
    for (bool abort : {false, true}) {
        hs_time(abort ? 40000 : 20000); const uint32_t start = AP_HAL::millis();
        HandshakeTestPort port; hs_test_port = &port; port.idle = false;
        AP_SpektrumSmartESC_Diagnostics d; hs_set(d, "ENABLE", 1); hs_set(d, "HS", 2); d.init(); d.update();
        EXPECT_EQ(port.starts, 0U); EXPECT_TRUE(port.sent.empty());
        hs_set(d, "LABACK", 15); d.update(); hs_set(d, "HS", 2); d.update();
        EXPECT_EQ(d.reader_status().state, AP_SpektrumSmartESC_Reader::State::PREPARED);
        hs_time(start + 1999); d.update(); EXPECT_TRUE(port.sent.empty());
        if (abort) {
            hs_set(d, "HS", -1); d.update(); EXPECT_EQ(port.finishes, 1U); EXPECT_TRUE(port.sent.empty());
        } else {
            hs_time(start + 2000); d.update(); EXPECT_TRUE(port.sent.empty()); // no bias, no TX
            port.idle = true; hs_time(start + 2400); d.update(); ASSERT_EQ(port.sent.size(), 1U);
            EXPECT_EQ(port.sent[0][3], 0x30); EXPECT_EQ(port.sent[0][4], 0x40);
            hs_time(start + 2402); port.done = true; port.tc = AP_HAL::millis(); d.update();
            hs_time(start + 2465); d.update(); ASSERT_EQ(port.sent.size(), 2U);
            hs_time(start + 2467); port.done = true; port.tc = AP_HAL::millis(); d.update();
            hs_time(start + 7000); d.update();
            EXPECT_EQ(d.reader_status().state, AP_SpektrumSmartESC_Reader::State::TIMEOUT);
            EXPECT_FALSE(port.owned); EXPECT_EQ(port.finishes, 1U);
        }
        EXPECT_TRUE(d.arming_blocked()); EXPECT_TRUE(SRV_Channels::get_emergency_stop());
    }
    hs_test_port = nullptr;
}
#endif
#else
TEST(SpektrumLab, DefaultBuildHasNoLabAuthorization)
{
    EXPECT_FALSE(AP_SPEKTRUM_SMART_ESC_LAB_ONLY);
}
#endif
AP_GTEST_MAIN()
