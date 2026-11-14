/* SPDX-License-Identifier: GPL-3.0-or-later */
// Host-only proof that the separate Pixhawk EarlyNative board cannot ARM,
// regardless of a valid 0x21 or a forced software ARM request.
// No IO driver, motor output, PWM, or physical ESC is linked in this test.
#include "../AP_SpektrumSmartESC_Motor.h"
#include "../AP_SpektrumSmartESC_ProductionProfile.h"
#include <cassert>
#include <cstdio>

using Motor = AP_SpektrumSmartESC_Motor;
struct Port : AP_SpektrumSmartESC_Bus::Port {
    bool complete = false;
    bool held = false;
    uint8_t last_type = 0;
    unsigned transmissions = 0;
    bool acquire() override { held = true; return true; }
    void relinquish() override { held = false; }
    bool simulated() const override { return false; }
    bool idle_high() const override { return true; }
    bool collision() const override { return false; }
    bool begin_tx(const uint8_t *p, uint8_t length) override {
        last_type = length > 1 ? p[1] : 0;
        transmissions++;
        complete = false;
        return true;
    }
    bool transmission_complete() const override { return complete; }
    bool release_rx() override { return true; }
};
struct Owner : Motor::Owner {
    Motor::Receipt r {};
    bool acquire(uint32_t epoch) override {
        r.epoch = epoch; r.io_boot = 123;
        r.main3_inhibited = r.failsafe_inhibited = r.aux7_reserved = true;
        return true;
    }
    Motor::Receipt receipt() const override { return r; }
    void release() override { r.epoch = 0; }
};

static void observe_arm_before_peer()
{
    Port p; Owner o;
    const auto prof=spektrum_production_profile();
    Motor m(p,o,prof.channels);
    assert(m.start(prof.qualification,true,0x40,0x12345678U,0U,0U));
    Motor::Demand d {};
    d.armed = d.rc_ok = d.control_ok = true;
    o.r.acquired_ms = d.acquired_ms = 1;
    m.update(d,1000U,1U);
    assert(m.status().fault == Motor::Fault::DISARMED);
    assert(m.status().state == Motor::State::FAULT);
    assert(p.transmissions == 0U);
    assert(!p.held);
}

static void observe_arm_after_valid_peer()
{
    Port p; Owner o;
    const auto prof=spektrum_production_profile();
    Motor m(p,o,prof.channels);
    assert(m.start(prof.qualification,true,0x40,0x12345678U,0U,0U));
    Motor::Demand d {};
    o.r.acquired_ms = d.acquired_ms = 50U;
    m.update(d,50000U,50U);
    assert(p.last_type == 0x21 && p.transmissions == 1U);
    p.complete = true;
    o.r.acquired_ms = d.acquired_ms = 52U;
    m.update(d,52000U,52U);
    const uint8_t reply[]={0x40,0x30,10,0,0,0x78,0x56,0x34,0x12};
    uint8_t frame[80] {};
    const uint8_t n=AP_SpektrumSmartESC::encode(0x21,reply,sizeof(reply),frame,sizeof(frame));
    for(uint8_t i=0;i<n;i++) m.feed(frame[i],53000U);
    o.r.acquired_ms = d.acquired_ms = 56U;
    m.update(d,56000U,56U);
    assert(p.last_type == 0x21 && p.transmissions == 2U);
    p.complete = true;
    o.r.acquired_ms = d.acquired_ms = 58U;
    m.update(d,58000U,58U);
    o.r.acquired_ms = d.acquired_ms = 59U;
    m.update(d,59000U,59U);
    assert(m.status().state == Motor::State::IDLE);
    assert(m.status().controls == 0);
    d.armed = d.rc_ok = d.control_ok = true;
    o.r.acquired_ms = d.acquired_ms = 60U;
    m.update(d,60000U,60U);
    assert(m.status().fault == Motor::Fault::DISARMED);
    assert(m.status().state == Motor::State::FAULT);
    assert(m.status().controls == 0);
    assert(!p.held);
}

int main()
{
    static_assert(AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL == 1,
                  "Needs inherited ArmedBench profile");
    static_assert(AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT == 1,
                  "Must exercise actual EarlyNative compile-time guard");
    observe_arm_before_peer();
    observe_arm_after_valid_peer();
    std::puts("2 EarlyNative forced-ARM safety tests passed (host only)");
}
