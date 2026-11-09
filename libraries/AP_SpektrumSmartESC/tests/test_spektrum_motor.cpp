/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Motor.h"
#include "../AP_SpektrumSmartESC_Plane.h"
#include "../AP_SpektrumSmartESC_ProductionProfile.h"
#include <cmath>
#include <cstring>
#include <limits>
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

using Motor = AP_SpektrumSmartESC_Motor;
using Codec = AP_SpektrumSmartESC_ChannelData;
using Engine = AP_SpektrumSmartESC;

struct MockPort : AP_SpektrumSmartESC_Bus::Port {
    bool fake = true, high = true, collide = false, complete = false, release_ok = true;
    bool held = false;
    uint8_t sent[80] {};
    uint8_t length = 0;
    unsigned starts = 0;
    bool timestamped = false;
    uint32_t completed_us = 0;
    bool acquire() override { held = true; return true; }
    void relinquish() override { held = false; }
    bool simulated() const override { return fake; }
    bool idle_high() const override { return high; }
    bool collision() const override { return collide; }
    bool begin_tx(const uint8_t *p, uint8_t n) override {
        memcpy(sent, p, n); length = n; starts++; complete = false; return true;
    }
    bool transmission_complete() const override { return complete; }
    bool completion_time_us(uint32_t &t) const override { t=completed_us; return timestamped; }
    bool release_rx() override { return release_ok; }
};
struct MockOwner : Motor::Owner {
    Motor::Receipt r;
    bool held = false;
    bool acquire(uint32_t epoch) override {
        r.epoch = epoch; r.io_boot = 123;
        r.main3_inhibited = r.failsafe_inhibited = r.aux7_reserved = true;
        held = true; return true;
    }
    Motor::Receipt receipt() const override { return r; }
    void release() override { held = false; } // Does not enable PWM.
};
static AP_SpektrumSmartESC_Commands::Profile calibration(bool handshake_only = false)
{
    AP_SpektrumSmartESC_Commands::Profile p;
    p.throttle_verified = p.reverse_verified = true;
    p.throttle_channel = 0; p.reverse_channel = 6;
    // Synthetic endpoints, NOT claimed Avian calibration/polarity/dwell.
    p.zero = 1000; p.full = 61000; p.forward = 2000; p.reverse = 60000; p.direction_dwell_ms = 44;
    p.handshake_only_bench = handshake_only;
    return p;
}
static Motor::Qualification qualification()
{
    Motor::Qualification q;
    q.electrical = q.protocol = q.endpoints = q.esc_loss_behavior = q.reverse_settings = true;
    return q;
}
struct Fixture {
    MockPort port;
    MockOwner owner;
    Motor motor;
    Motor::Demand d;
    uint32_t t = 0;
    uint32_t base_us = 0, base_ms = 0, last_heartbeat_us = 0;
    Fixture(uint32_t us = 0, uint32_t ms = 0, bool handshake_only = false) :
        motor(port, owner, calibration(handshake_only)), base_us(us), base_ms(ms) {}
    void tick(uint32_t us) {
        t = us;
        owner.r.acquired_ms = d.acquired_ms = d.ground_ms = d.stopped_ms = base_ms + t / 1000;
        motor.update(d, base_us + t, base_ms + t / 1000);
    }
    void feed(const uint8_t *p, uint8_t n, uint32_t us) {
        for (uint8_t i = 0; i < n; i++) { motor.feed(p[i], base_us + us); }
    }
    void discover() {
        owner.r.acquired_ms = base_ms;
        EXPECT_TRUE(motor.start(qualification(), true, 0x40, 0x1234, base_us, base_ms));
        tick(50000); EXPECT_EQ(port.sent[3], 0x31); EXPECT_EQ(port.sent[4], 0x40);
        port.complete = true; tick(52000);
        const uint8_t payload[] = {0x40,0x31,1,0,0,0x78,0x56,0x34,0x12};
        uint8_t p[80]; auto n = Engine::encode(0x21,payload,sizeof(payload),p,sizeof(p));
        feed(p,n,53000); tick(55000);
        EXPECT_EQ(port.sent[4],0xFF);
        port.complete = true; tick(57000); tick(58000);
        EXPECT_EQ(motor.status().state,Motor::State::IDLE);
        last_heartbeat_us = 58000;
        d.armed = d.rc_ok = d.control_ok = d.ground_authorized = d.stopped = d.reverse_permitted = true;
    }
    void reply(uint32_t us) {
        uint8_t payload[17] {}; payload[0] = 0x31; payload[1] = 0x20;
        uint8_t p[80]; auto n = Engine::encode(0x80,payload,sizeof(payload),p,sizeof(p));
        feed(p,n,us);
    }
    void cycle(uint32_t us) {
        // A real connected Avian emits authenticated 0x21 heartbeat frames
        // regularly; sensor 0x80 frames have no sender ID and must NOT renew
        // that timer. Inject independent authentic peer evidence in the mock.
        if (uint32_t(us - last_heartbeat_us) >= 40000U) {
            const uint8_t payload[] = {0x40,0x31,1,0,0,0x78,0x56,0x34,0x12};
            uint8_t heartbeat[80] {};
            const auto n = Engine::encode(0x21,payload,sizeof(payload),heartbeat,sizeof(heartbeat));
            // Keep the peer's half-duplex announcement at least 2ms before
            // the next controller TX slot. A frame ending 1ms earlier makes
            // the real UART correctly defer TX, but invalidates mock tests
            // that demand exactly one control packet per simulated tick.
            feed(heartbeat,n,us-3000U);
            last_heartbeat_us = us-3000U;
        }
        tick(us); port.complete = true; tick(us + 2000); reply(us + 3000);
    }
};

TEST(SpektrumMotor, PublishedRevKChannelVector)
{
    // Rev K 7.7.1 published bytes; CRC independently calculated with
    // Python binascii.crc_hqx(payload, 0), not this driver's encoder.
    const uint8_t expected[] = {0xA6,0xCD,0x1C,0,0x30,0x58,0x0B,0,0x37,6,0,0,
        0xA0,0x2A,0,0x80,4,0x80,0xFC,0x7F,0x54,0xD5,0xA0,0x2A,0xA0,0x2A,0xD3,0x5D};
    uint16_t v[32] {}; v[0]=10912; v[1]=32768; v[2]=32772; v[4]=32764;
    v[5]=54612; v[9]=v[10]=10912;
    uint8_t p[80]; EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0x30,88,11,0x637,v,p,80),28);
    EXPECT_EQ(memcmp(p,expected,sizeof(expected)),0);
}
TEST(SpektrumMotor, CodecBoundsOrderingAndFailsafe)
{
    uint16_t v[32] {}; v[31]=65532;
    uint8_t p[80];
    EXPECT_EQ(Codec::encode(Codec::Kind::Failsafe,0,-80,123,0x80000001U,v,p,80),18);
    EXPECT_EQ(p[3],1); EXPECT_EQ(p[12],0); EXPECT_EQ(p[14],0xFC); EXPECT_EQ(p[15],0xFF);
    EXPECT_EQ(Engine::crc16(p,18),0);
    EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0,0,0,0xFFFFFFFFU,v,p,80),78);
    EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0,0,0,1,nullptr,p,80),0);
    v[0]=65535; EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0,0,0,1,v,p,80),0);
    v[0]=4; EXPECT_EQ(Codec::encode(Codec::Kind::Normal,0,0,0,1,v,p,15),0);
    EXPECT_EQ(Codec::encode(Codec::Kind(2),0,0,0,1,v,p,80),0);
}
TEST(SpektrumMotor, PhysicalPortAlwaysBlocked)
{
    Fixture f; f.port.fake=false;
    EXPECT_FALSE(Motor::physical_control_available());
    EXPECT_FALSE(f.motor.start(qualification(),true,0x40,1,0,0));
    EXPECT_EQ(f.port.starts,0U); EXPECT_FALSE(f.owner.held);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::QUALIFICATION);
}
TEST(SpektrumMotor, DedicatedFCMasterAndDisarmedRCBoot)
{
    // Spektrum's dedicated FC0 master identity 0x30 was verified against
    // physical Avian ID 0x40. The old 0x31 simulator vector stays intact.
    EXPECT_EQ(Motor::native_fc_id(false), 0x30);
    EXPECT_EQ(Motor::native_fc_id(true), 0x31);
    Fixture f;
    f.discover();
    f.d.armed = false; f.d.rc_ok = false;
    f.d.control_ok = false; f.d.failsafe = true;
    f.tick(70000);
    EXPECT_EQ(f.motor.status().fault, Motor::Fault::NONE);
    EXPECT_EQ(f.port.starts, 3U); // CH1=zero only; keeps ESC running without RC.
    // Let the first DISARMED CH1=zero packet reach physical TC and
    // accept its simulated ESC zero-throttle reply before any arming edge.
    // The previous test armed while that TX was still pending and then
    // asserted TC completion as if arming had already been qualified.
    f.port.complete = true;
    f.tick(72000);
    EXPECT_EQ(f.motor.status().throttle, 1000);
    f.reply(73000);
    EXPECT_EQ(f.motor.status().fault, Motor::Fault::NONE);
    // RP4TD becomes available only AFTER FC/ESC established the link.
    // No auto-arm: maintain zero in the next disarmed control slot.
    f.d.failsafe = false;
    f.d.control_ok = true;
    f.d.rc_ok = true;
    f.cycle(86000);
    EXPECT_EQ(f.motor.status().throttle, 1000);
    EXPECT_EQ(f.motor.status().fault, Motor::Fault::NONE);
    EXPECT_TRUE(f.motor.ready_to_arm(89, 89000));
}

TEST(SpektrumMotor, QualificationAndStartupNeutral)
{
    Fixture f;
    EXPECT_FALSE(f.motor.start({},true,0x40,1,0,0));
    EXPECT_FALSE(f.motor.start(qualification(),false,0x40,1,0,0));
    f.discover(); f.d.magnitude=0.5f; f.tick(70000);
    EXPECT_EQ(f.port.starts,2U); // Handshakes only, not a resumed throttle.
    f.d.magnitude=0; f.cycle(81000);
    EXPECT_EQ(f.motor.status().throttle,1000);
}
TEST(SpektrumMotor, ThrottleQuartersAndChannelIsolation)
{
    Fixture f; f.discover(); f.cycle(70000);
    for (unsigned i=1;i<=4;i++) {
        f.d.magnitude=float(i)/4; f.cycle(70000+i*13000);
        EXPECT_EQ(f.motor.status().throttle,1000+i*15000);
        EXPECT_EQ(f.port.length,18); EXPECT_EQ(f.port.sent[8],0x41);
        EXPECT_EQ(f.port.sent[9],0); EXPECT_EQ(f.port.sent[10],0); EXPECT_EQ(f.port.sent[11],0);
        EXPECT_EQ(f.motor.status().direction,2000);
        EXPECT_EQ(f.port.sent[4],0x40); // Reply request, not SERVO4.
    }
}
TEST(SpektrumMotor, SixPercentMotorTurnIsOnlyATestDemand)
{
    // This is a TEST SCRIPT request (0 -> 6% -> 0), not a motor limit.
    // The mock transport never drives a physical ESC.
    Fixture f;
    f.discover();
    f.cycle(70000); // Initial neutral command with accepted mock reply.
    EXPECT_EQ(f.motor.status().throttle,1000);

    f.d.magnitude=0.06f; // Explicit test demand only.
    f.cycle(83000);
    EXPECT_EQ(f.motor.status().throttle,4600);

    f.d.magnitude=0.0f;
    f.cycle(96000);
    EXPECT_EQ(f.motor.status().throttle,1000);
}

TEST(SpektrumMotor, NormalMotorPathRetainsFullConfiguredThrottleRange)
{
    // Separate instance from the 6% turn test: no permanent code cap.
    Fixture f;
    f.discover();
    f.cycle(70000);
    f.d.magnitude=1.0f;
    f.cycle(83000);
    EXPECT_EQ(f.motor.status().throttle,61000);
}

TEST(SpektrumMotor, ReverseDwellAndReturn)
{
    Fixture f; f.discover(); f.cycle(70000);
    f.d.magnitude=0.75f; f.cycle(83000);
    f.d.reverse=true; f.d.stopped=false; f.cycle(96000);
    EXPECT_EQ(f.motor.status().throttle,1000); EXPECT_FALSE(f.motor.status().reverse);
    f.d.stopped=true;
    for (unsigned t=109000;t<161000;t+=13000) {
        f.cycle(t); EXPECT_FALSE(f.motor.status().reverse);
    }
    f.cycle(161000); EXPECT_TRUE(f.motor.status().reverse); EXPECT_EQ(f.motor.status().throttle,1000);
    f.cycle(174000); EXPECT_EQ(f.motor.status().throttle,1000); // Post-switch zero dwell.
    f.cycle(213000); EXPECT_EQ(f.motor.status().throttle,46000);
    f.d.reverse=false; f.d.stopped=false; f.cycle(226000);
    EXPECT_TRUE(f.motor.status().reverse); EXPECT_EQ(f.motor.status().throttle,1000);
    f.d.stopped=true;
    for (unsigned t=239000;t<=291000;t+=13000) { f.cycle(t); }
    EXPECT_FALSE(f.motor.status().reverse); EXPECT_EQ(f.motor.status().throttle,1000);
}
TEST(SpektrumMotor, SafetyFaultsLatchAndNeverReenablePWM)
{
    for (unsigned scenario=0;scenario<9;scenario++) {
        Fixture f; f.discover(); f.cycle(70000);
        Motor::Fault expected=Motor::Fault::NONE;
        switch (scenario) {
        case 0: f.d.armed=false; expected=Motor::Fault::DISARMED; break;
        case 1: f.d.rc_ok=false; expected=Motor::Fault::RC_LOSS; break;
        case 2: f.d.control_ok=false; expected=Motor::Fault::CONTROL_LOSS; break;
        case 3: f.d.failsafe=true; expected=Motor::Fault::CONTROL_LOSS; break;
        case 4:
            // Ground evidence is needed for reverse, not normal forward
            // throttle in flight. Preserve this latched safety fault test
            // by actually requesting the ground-restricted operation.
            f.d.ground_authorized=false;
            f.d.reverse=true;
            expected=Motor::Fault::GROUND;
            break;
        case 5: f.d.magnitude=std::numeric_limits<float>::quiet_NaN(); expected=Motor::Fault::INVALID_DEMAND; break;
        case 6: f.d.magnitude=-0.1f; expected=Motor::Fault::INVALID_DEMAND; break;
        case 7: f.d.reverse=true; f.d.reverse_permitted=false; expected=Motor::Fault::REVERSE_DENIED; break;
        case 8: f.port.collide=true; expected=Motor::Fault::UART; break;
        }
        f.tick(83000); EXPECT_EQ(f.motor.status().fault,expected);
        EXPECT_FALSE(f.port.held); EXPECT_FALSE(f.owner.held);
        EXPECT_TRUE(f.owner.r.main3_inhibited); // No automatic fallback/unmute.
        auto count=f.port.starts; f.d={}; f.tick(100000); EXPECT_EQ(f.port.starts,count);
    }
}
TEST(SpektrumMotor, IOResetAndFailsafeInhibit)
{
    for (unsigned i=0;i<4;i++) {
        Fixture f; f.discover(); f.cycle(70000);
        if (i==0) { f.owner.r.io_boot++; }
        if (i==1) { f.owner.r.main3_inhibited=false; }
        if (i==2) { f.owner.r.failsafe_inhibited=false; }
        if (i==3) { f.owner.r.aux7_reserved=false; }
        f.tick(83000); EXPECT_EQ(f.motor.status().fault,Motor::Fault::OWNERSHIP);
    }
}
TEST(SpektrumMotor, TimeoutInvalidReplyAndNoRetry)
{
    Fixture f; f.discover(); f.tick(70000);
    EXPECT_EQ(f.motor.status().controls,0U); // FIFO queued is NOT TC-completed.
    f.port.complete=true; f.tick(72000); f.tick(77000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE); // One missing initial reply is tolerated.
    f.tick(175000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS); // No reply after 100ms.
    const auto count=f.port.starts; f.tick(200000); EXPECT_EQ(f.port.starts,count);
    Fixture tx; tx.discover(); tx.tick(70000); tx.tick(73000);
    EXPECT_EQ(tx.motor.status().fault,Motor::Fault::TX_TIMEOUT);
}
TEST(SpektrumMotor, EchoAndReleaseFailure)
{
    Fixture f; f.discover(); f.tick(70000);
    f.feed(f.port.sent,f.port.length,70100);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::UART);
    Fixture release; release.discover(); release.tick(70000); release.port.release_ok=false;
    release.port.complete=true; release.tick(72000);
    EXPECT_EQ(release.motor.status().fault,Motor::Fault::TURNAROUND);
    EXPECT_FALSE(release.motor.start(qualification(),true,0x40,1,80000,80));
}
TEST(SpektrumMotor, TelemetryFreshnessAndCorruptResponse)
{
    Fixture f; f.discover(); f.cycle(70000);
    Engine::Telemetry telemetry;
    EXPECT_TRUE(f.motor.telemetry(telemetry,73000));
    EXPECT_FALSE(f.motor.telemetry(telemetry,123000));
    f.tick(83000); f.port.complete=true; f.tick(85000);
    uint8_t p[22] {}; p[0]=0xA6; p[1]=0x80; p[2]=22;
    f.feed(p,22,86000); f.tick(90000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    f.tick(170000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS); // CRC-bad reply cannot renew telemetry freshness.
}
TEST(SpektrumMotor, StaleDemandAndReceipt)
{
    Fixture f; f.discover(); f.cycle(70000);
    f.owner.r.acquired_ms=130; f.d.acquired_ms=70;
    f.motor.update(f.d,130000,130); EXPECT_EQ(f.motor.status().fault,Motor::Fault::CONTROL_LOSS);
    Fixture owner; owner.discover(); owner.cycle(70000);
    owner.motor.update(owner.d,130000,130); EXPECT_EQ(owner.motor.status().fault,Motor::Fault::OWNERSHIP);
}

TEST(SpektrumMotor, IndependentMillisSurvivesMicrosWrap)
{
    Fixture f(0xFFFF0000U,5000); f.discover(); f.cycle(70000);
    f.cycle(83000);
    EXPECT_EQ(f.port.starts,4U);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
}

TEST(SpektrumMotor, BusBusyNeverRetriesForever)
{
    Fixture f; f.discover(); f.port.high=false;
    f.tick(70000);
    // BUSY/LOW since last handshake TC: zero control frames completed.
    // Sensor watchdog MUST NOT be the sole guard, since controls==0.
    EXPECT_EQ(f.motor.status().controls,0U);
    f.tick(140000); // <100ms since actual final handshake TC
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    f.tick(159000); // >100ms since 57ms final handshake TC
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS);
    f.tick(210000); // A fault cannot revive an output or reacquire TX.
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS);
    EXPECT_EQ(f.port.starts,2U);
    Fixture noesc;
    EXPECT_TRUE(noesc.motor.start(qualification(),true,0x40,1,0,0));
    noesc.tick(50000); noesc.port.complete=true; noesc.tick(52000); noesc.tick(102000);
    EXPECT_EQ(noesc.motor.status().fault,Motor::Fault::ESC_LOSS);
    EXPECT_EQ(noesc.port.starts,1U);
}

TEST(SpektrumMotor, FinalPlaneAdapterPreservesRangeAndDirection)
{
    AP_SpektrumSmartESC_Plane::Snapshot s;
    s.servo_index=2; s.function=70; s.minimum=1100; s.maximum=1900; s.trim=1500;
    s.pwm=1300; s.armed=s.rc_ok=s.control_ok=true;
    Motor::Demand d;
    EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,10,d));
    EXPECT_TRUE(std::fabs(d.magnitude-0.25f)<0.0001f);
    EXPECT_EQ(d.acquired_ms,10U); EXPECT_TRUE(d.armed);
    EXPECT_FALSE(d.stopped); EXPECT_FALSE(d.ground_authorized); EXPECT_FALSE(d.reverse);
    s.reversed=true; s.pwm=1700;
    EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,11,d));
    EXPECT_TRUE(std::fabs(d.magnitude-0.25f)<0.0001f);
    s.pwm=0; EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,12,d));
    EXPECT_TRUE(std::fabs(d.magnitude)<0.0001f); EXPECT_FALSE(d.armed);
}

TEST(SpektrumMotor, PlaneAdapterCannotUseOtherOutputsOrImplicitReverse)
{
    AP_SpektrumSmartESC_Plane::Snapshot s;
    s.servo_index=0; s.function=4; s.minimum=1000; s.trim=1400; s.maximum=2000; s.pwm=1200;
    Motor::Demand d;
    EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,0,d));
    s.servo_index=2; EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,0,d));
    s.function=70; s.bipolar=true;
    EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,0,d)); // Negative alone never selects CH7.
    s.reverse_selected=true;
    EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,0,d)); // Unconfigured raw RC7 equivalent.
    s.reverse_configured=true; s.reverse_permitted=true;
    EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,1,d));
    EXPECT_TRUE(d.reverse); EXPECT_TRUE(std::fabs(d.magnitude-0.5f)<0.0001f);
    EXPECT_FALSE(d.ground_authorized); EXPECT_FALSE(d.stopped);
    s.reverse_permitted=false;
    EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,2,d));
}

TEST(SpektrumMotor, AcquiredResponseMayPrecedeMainThreadTCAcknowledgement)
{
    Fixture f; f.discover(); f.tick(70000);
    f.port.timestamped=true; f.port.completed_us=72000; f.port.complete=true;
    f.tick(90000); // Response is already in the hardware ring at this point.
    f.reply(73000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    EXPECT_EQ(f.motor.status().telemetry,1U);
}

TEST(SpektrumMotor, DisarmedIdleDoesNotGrantArmingAtNonzeroDemand)
{
    Fixture f; f.discover(); f.d.armed=false; f.cycle(70000);
    EXPECT_EQ(f.motor.status().throttle,1000);
    f.d.armed=true; f.d.magnitude=0.5f; f.tick(83000);
    EXPECT_EQ(f.port.starts,3U); // No command at the nonzero arming edge.
    f.d.magnitude=0; f.cycle(96000);
    f.d.magnitude=0.5f; f.cycle(109000);
    EXPECT_EQ(f.motor.status().throttle,31000);
}

TEST(SpektrumMotor, ArmingNeedsCompletedNeutralReplyAndFreshOwnership)
{
    Fixture f;
    EXPECT_FALSE(f.motor.ready_to_arm(0,0));
    f.discover(); f.d.armed=false;
    EXPECT_FALSE(f.motor.ready_to_arm(58,58000));
    f.tick(70000); f.port.complete=true; f.tick(72000);
    EXPECT_FALSE(f.motor.ready_to_arm(72,72000));
    f.reply(73000);
    EXPECT_TRUE(f.motor.ready_to_arm(73,73000));
    f.owner.r.main3_inhibited=false;
    EXPECT_FALSE(f.motor.ready_to_arm(73,73000));
    f.owner.r.main3_inhibited=true;
    f.motor.disable();
    EXPECT_FALSE(f.motor.ready_to_arm(73,73000));
}

TEST(SpektrumMotor, ControlCadenceIsMeasuredStartToStartNotAfterTC)
{
    Fixture f; f.discover(); f.cycle(70000);
    f.tick(81000);
    EXPECT_EQ(f.port.starts,4U); // 11 ms from TX start, despite TC at 72 ms.
}

TEST(SpektrumMotor, FinalSignedDemandMustAgreeWithExplicitDirection)
{
    AP_SpektrumSmartESC_Plane::Snapshot s;
    s.servo_index=2; s.function=70; s.minimum=1000; s.trim=1500; s.maximum=2000;
    s.bipolar=true; s.reverse_configured=true; s.reverse_permitted=true;
    s.reverse_selected=true; s.pwm=1750;
    Motor::Demand d;
    EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,1,d));
    s.pwm=1500; EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,2,d));
    EXPECT_TRUE(d.reverse); EXPECT_TRUE(std::fabs(d.magnitude)<0.0001f);
    s.pwm=1250; EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,3,d));
    EXPECT_TRUE(std::fabs(d.magnitude-0.5f)<0.0001f);
    s.bipolar=false; EXPECT_FALSE(AP_SpektrumSmartESC_Plane::convert(s,4,d));
}

TEST(SpektrumMotor, FinalPlaneQuartersBecomeIndependentChannelOneValues)
{
    for (unsigned quarter=0;quarter<=4;quarter++) {
        AP_SpektrumSmartESC_Plane::Snapshot s;
        s.servo_index=2; s.function=70; s.minimum=1000; s.trim=1500; s.maximum=2000;
        s.pwm=1000+250*quarter; s.armed=s.rc_ok=s.control_ok=true;
        Motor::Demand d;
        EXPECT_TRUE(AP_SpektrumSmartESC_Plane::convert(s,73,d));
        EXPECT_TRUE(std::fabs(d.magnitude-0.25f*quarter)<0.0001f);
        Fixture f; f.discover(); f.cycle(70000);
        f.d.magnitude=d.magnitude; f.cycle(83000);
        Engine::Channels wire;
        EXPECT_TRUE(Engine::decode_channels(f.port.sent,f.port.length,wire));
        EXPECT_EQ(wire.mask,0x41U);
        EXPECT_EQ(wire.values[0],1000+15000*quarter);
        EXPECT_EQ(wire.values[6],2000);
        EXPECT_EQ(Engine::crc16(f.port.sent,f.port.length),0);
    }
}

TEST(SpektrumMotor, RecurringUnsolicitedHandshakeIsNotAProtocolFault)
{
    Fixture f; f.discover(); f.cycle(70000);
    const uint8_t payload[] = {0x40,0x31,1,0,0,0x78,0x56,0x34,0x12};
    uint8_t p[80] {};
    const auto n = Engine::encode(0x21,payload,sizeof(payload),p,sizeof(p));
    EXPECT_EQ(n,14);
    f.feed(p,n,74000); // Real ESC sends 0x21 even after establishing link.
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    f.tick(83000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
}

TEST(SpektrumMotor, HandshakeOnlyBenchDoesNotPretendToHaveTelemetry)
{
    Fixture f(0,0,true); f.discover();
    f.d.armed=false;
    f.tick(70000); f.port.complete=true; f.tick(72000);
    f.tick(79000); // No 0x80 reply, but last verified peer is fresh.
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    EXPECT_EQ(f.motor.status().telemetry,0U);
    EXPECT_FALSE(f.motor.ready_to_arm(79,79000));
    const uint8_t payload[] = {0x40,0x31,1,0,0,0x78,0x56,0x34,0x12};
    uint8_t p[80] {};
    const auto n = Engine::encode(0x21,payload,sizeof(payload),p,sizeof(p));
    f.feed(p,n,80000);
    f.tick(83000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    f.port.complete=true; f.tick(85000); // Still waiting (13ms): no new TC.
    f.tick(90000); // Reply expires (18ms); a second zero 0xCD TX is STARTED.
    // The mock UART marks begin_tx() as incomplete until explicitly TC'd.
    // Without this, jumping straight to 200ms correctly faults TX_TIMEOUT
    // for a stalled second TX, not ESC_LOSS for an old 0x21 peer.
    EXPECT_EQ(f.motor.status().fault, Motor::Fault::NONE);
    EXPECT_EQ(f.motor.status().controls, 1U);
    f.port.complete = true;
    f.tick(92000); // Realistic completion of the second zero control.
    EXPECT_EQ(f.motor.status().controls, 2U);
    EXPECT_EQ(f.motor.status().fault, Motor::Fault::NONE);
    // 98ms after last hardware TC (<100ms TX-progress bound), but 110ms
    // since the last authenticated 0x21. The handshake-only bench must
    // still end by its OWN peer deadline. A missing 0x80 grants no arming.
    f.tick(190000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS);
    EXPECT_FALSE(f.motor.ready_to_arm(190, 190000));
}

TEST(SpektrumMotor, DisarmedZeroBootstrapsWithoutFabricatedStoppedEvidence)
{
    Fixture f; f.discover();
    f.d.armed = false;
    f.d.stopped = false; // No motor sensor until first real 0xCD reply.
    f.d.ground_authorized = false;
    f.cycle(70000);
    EXPECT_EQ(f.port.length,18);
    EXPECT_EQ(f.motor.status().throttle,1000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    EXPECT_FALSE(f.motor.ready_to_arm(73,73000)); // No zero-RPM evidence gate yet.
    f.d.stopped = true; // Independent mock of new real ESC RPM observer.
    f.d.ground_authorized = true;
    f.cycle(83000);
    EXPECT_TRUE(f.motor.ready_to_arm(86,86000));
}

TEST(SpektrumMotor, ForwardThrustDoesNotRequireGroundButReverseDoes)
{
    Fixture f; f.discover(); f.cycle(70000);
    f.d.ground_authorized = false; f.d.stopped = false;
    f.d.magnitude = 0.5f;
    f.cycle(83000); // Simulates forward flight; never gate it on GROUND.
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::NONE);
    EXPECT_EQ(f.motor.status().throttle,31000);
    Fixture rev; rev.discover(); rev.cycle(70000);
    rev.d.ground_authorized = false; rev.d.reverse = true;
    rev.tick(83000);
    EXPECT_EQ(rev.motor.status().fault,Motor::Fault::GROUND);
}

TEST(SpektrumMotor, ProductionSourceCannotAccidentallyArm)
{
    // These are synthetic values only, NOT physical Avian evidence.
    auto p = spektrum_production_profile();
    EXPECT_FALSE(p.valid());
    p.operator_qualified = true;
    p.qualification = qualification();
    p.qualification.io_firmware_crc = 0x6DBBAD71U;
    p.channels = calibration();
    EXPECT_TRUE(p.valid()); // Structural check on synthetic fixture alone.
    EXPECT_FALSE(Motor::physical_control_available()); // Real TX remains blocked.
    p.channels.reverse_channel = 0;
    EXPECT_FALSE(p.valid());
    p.channels = calibration();
    p.qualification.esc_loss_behavior = false;
    EXPECT_FALSE(p.valid());
    p.qualification.esc_loss_behavior = true;
    p.qualification.io_firmware_crc = 0U;
    EXPECT_FALSE(p.valid());
    p.qualification.io_firmware_crc = 0x6DBBAD71U;
    p.operator_qualified = false;
    EXPECT_FALSE(p.valid());
}

TEST(SpektrumMotor, NativeArmRequiresActualReportedZeroAndStoppedRPM)
{
    Fixture f; f.discover();
    f.d.armed = false;
    f.tick(70000); f.port.complete=true; f.tick(72000);
    // A TC-completed idle command without ESC telemetry cannot grant arming.
    EXPECT_FALSE(f.motor.ready_to_arm(72,72000));
    uint8_t payload[17] {};
    payload[0] = 0x31; payload[1] = 0x20;
    payload[15] = 20; // 10 percent throttle as reported by ESC
    uint8_t frame[80] {};
    const auto n = Engine::encode(0x80,payload,sizeof(payload),frame,sizeof(frame));
    f.feed(frame,n,73000);
    EXPECT_FALSE(f.motor.ready_to_arm(73,73000));
    EXPECT_EQ(f.motor.status().telemetry,1U);
}

TEST(SpektrumMotor, SensorCannotImpersonateAuthenticatedPeer)
{
    Fixture f; f.discover(); f.cycle(70000);
    // Last genuine device 0x40 heartbeat remains at 58000us.
    // The sensor reply at 73000us has NO source ID and cannot extend it.
    f.tick(216000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::ESC_LOSS);
}

TEST(SpektrumMotor, WrongSensorReplyIsNotSuccessfulControlAck)
{
    Fixture f; f.discover();
    f.tick(70000); f.port.complete=true; f.tick(72000);
    uint8_t payload[17] {};
    payload[0] = 0x31; payload[1] = 0x21; // NOT ESC sensor 0x20
    uint8_t frame[80] {};
    const auto n = Engine::encode(0x80,payload,sizeof(payload),frame,sizeof(frame));
    f.feed(frame,n,73000);
    EXPECT_EQ(f.motor.status().fault,Motor::Fault::PROTOCOL);
}

#ifdef SPKTSMART_STANDALONE_TEST
int main()
{
    PublishedRevKChannelVector(); CodecBoundsOrderingAndFailsafe(); PhysicalPortAlwaysBlocked();
    QualificationAndStartupNeutral(); DedicatedFCMasterAndDisarmedRCBoot(); ThrottleQuartersAndChannelIsolation(); SixPercentMotorTurnIsOnlyATestDemand(); NormalMotorPathRetainsFullConfiguredThrottleRange(); ReverseDwellAndReturn();
    SafetyFaultsLatchAndNeverReenablePWM(); IOResetAndFailsafeInhibit(); TimeoutInvalidReplyAndNoRetry();
    EchoAndReleaseFailure(); TelemetryFreshnessAndCorruptResponse(); StaleDemandAndReceipt();
    IndependentMillisSurvivesMicrosWrap();
    BusBusyNeverRetriesForever();
    FinalPlaneAdapterPreservesRangeAndDirection(); PlaneAdapterCannotUseOtherOutputsOrImplicitReverse();
    AcquiredResponseMayPrecedeMainThreadTCAcknowledgement(); DisarmedIdleDoesNotGrantArmingAtNonzeroDemand();
    ArmingNeedsCompletedNeutralReplyAndFreshOwnership();
    ControlCadenceIsMeasuredStartToStartNotAfterTC();
    FinalSignedDemandMustAgreeWithExplicitDirection(); FinalPlaneQuartersBecomeIndependentChannelOneValues();
    RecurringUnsolicitedHandshakeIsNotAProtocolFault();
    HandshakeOnlyBenchDoesNotPretendToHaveTelemetry();
    DisarmedZeroBootstrapsWithoutFabricatedStoppedEvidence();
    ForwardThrustDoesNotRequireGroundButReverseDoes();
    ProductionSourceCannotAccidentallyArm(); NativeArmRequiresActualReportedZeroAndStoppedRPM();
    SensorCannotImpersonateAuthenticatedPeer(); WrongSensorReplyIsNotSuccessfulControlAck();
    std::puts("33 prepared master/command safety tests passed (mock transport only)");
}
#else
AP_GTEST_MAIN()
#endif
