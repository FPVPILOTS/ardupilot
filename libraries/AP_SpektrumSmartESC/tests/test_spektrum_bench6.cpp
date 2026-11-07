/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Bench6.h"
#include "../AP_SpektrumSmartESC_Bench6Wire.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using Gate = AP_SpektrumSmartESC_Bench6;

static Gate::Input safe()
{
    Gate::Input i;
    i.operator_enabled = true;
    i.external_kill_ready = true;
    i.fixture_verified = true;
    i.control_fresh = true;
    i.link_fresh = true;
    i.reverse_config_verified = true;
    i.stopped = true;
    return i;
}

static void no_arming_or_output_without_initial_neutral()
{
    Gate gate;
    auto i = safe();
    i.demand = 0.03f;
    auto o = gate.evaluate(i, 10);
    assert(!o.publishable && o.magnitude == 0 && !gate.enabled());
    i.demand = 0;
    o = gate.evaluate(i, 11);
    assert(o.publishable && o.magnitude == 0 && gate.enabled());
}

static void throttle_is_hard_capped()
{
    Gate gate; auto i = safe();
    gate.evaluate(i, 0);
    i.demand = 1.0f; i.stopped = false;
    auto o = gate.evaluate(i, 10);
    assert(o.publishable && o.clipped && o.magnitude == Gate::MAX_MAGNITUDE);
    assert(!o.reverse && o.reason == Gate::Reason::FORWARD);
    i.demand = 0.05f;
    o = gate.evaluate(i, 11);
    assert(!o.clipped && std::fabs(o.magnitude - 0.05f) < 0.00001f);
}

static void reverse_needs_neutral_confirmed_stop_and_two_dwells()
{
    Gate gate; auto i = safe(); gate.evaluate(i, 0);
    i.demand = 0.06f; i.stopped = false; gate.evaluate(i, 10);
    i.reverse = true;
    auto o = gate.evaluate(i, 20);
    assert(o.publishable && o.magnitude == 0 && !o.reverse);
    assert(o.reason == Gate::Reason::WAIT_NEUTRAL);
    i.demand = 0;
    o = gate.evaluate(i, 30);
    assert(o.reason == Gate::Reason::WAIT_STOP && !o.reverse);
    i.stopped = true;
    o = gate.evaluate(i, 100);
    assert(o.reason == Gate::Reason::WAIT_DWELL && !o.reverse);
    o = gate.evaluate(i, 599);
    assert(!o.reverse && o.magnitude == 0 && o.publishable);
    o = gate.evaluate(i, 600);
    assert(o.reverse && o.magnitude == 0 && o.publishable);
    o = gate.evaluate(i, 1099);
    assert(o.magnitude == 0 && o.reverse && o.publishable);
    o = gate.evaluate(i, 1100);
    assert(o.magnitude == 0 && o.reverse && o.publishable);
    i.demand = 0.99f;
    o = gate.evaluate(i, 1110);
    assert(o.publishable && o.reverse && o.clipped);
    assert(o.magnitude == Gate::MAX_MAGNITUDE);
}

static void cannot_reverse_without_configuration()
{
    Gate gate; auto i = safe(); gate.evaluate(i, 0);
    i.reverse = true; i.reverse_config_verified = false;
    auto o = gate.evaluate(i, 2);
    assert(!o.publishable && o.magnitude == 0 && gate.faulted());
    i.reverse_config_verified = true;
    o = gate.evaluate(i, 900);
    assert(!o.publishable && o.reason == Gate::Reason::FAULT);
}

static void loss_of_any_interlock_latches()
{
    for (unsigned n=0; n<5; n++) {
        Gate gate; auto i = safe(); gate.evaluate(i, 0);
        switch (n) {
            case 0: i.operator_enabled = false; break;
            case 1: i.external_kill_ready = false; break;
            case 2: i.fixture_verified = false; break;
            case 3: i.control_fresh = false; break;
            case 4: i.link_fresh = false; break;
        }
        auto o = gate.evaluate(i, 10);
        assert(!o.publishable && o.magnitude == 0 && gate.faulted());
        i = safe(); o = gate.evaluate(i, 20);
        assert(!o.publishable && o.reason == Gate::Reason::FAULT);
    }
}

static void reject_nan_infinity_and_negative()
{
    for (unsigned n=0; n<3; n++) {
        Gate gate; auto i = safe(); gate.evaluate(i, 0);
        if (n == 0) i.demand = std::numeric_limits<float>::quiet_NaN();
        if (n == 1) i.demand = std::numeric_limits<float>::infinity();
        if (n == 2) i.demand = -0.0001f;
        auto o = gate.evaluate(i, 10);
        assert(!o.publishable && o.magnitude == 0 && gate.faulted());
    }
}

static void lost_stop_evidence_resets_dwell()
{
    Gate gate; auto i = safe(); gate.evaluate(i, 0);
    i.reverse = true; i.stopped = true; gate.evaluate(i, 10);
    i.stopped = false; auto o = gate.evaluate(i, 300);
    assert(o.reason == Gate::Reason::WAIT_STOP && !o.reverse);
    i.stopped = true; gate.evaluate(i, 800);
    o = gate.evaluate(i, 1299);
    assert(!o.reverse);
    o = gate.evaluate(i, 1300);
    assert(o.reverse && o.magnitude == 0);
}

static void timestamp_wraparound_is_handled()
{
    Gate gate; auto i = safe(); gate.evaluate(i, 0);
    i.reverse = true; gate.evaluate(i, 0xFFFFFF00U);
    auto o = gate.evaluate(i, 0x000000F4U);
    assert(o.reverse && o.magnitude == 0); // exactly +500 ms
}

static void physical_motor_wire_value_is_capped()
{
    // Synthetic endpoints: an actual Avian endpoint is NOT asserted.
    using Wire = AP_SpektrumSmartESC_Bench6Wire;
    const uint16_t zero = 1000, full = 61000;
    assert(Wire::throttle(0.0f, zero, full) == 1000);
    assert(Wire::throttle(0.03f, zero, full) == 2800);
    assert(Wire::throttle(0.06f, zero, full) == 4600);
    assert(Wire::throttle(0.50f, zero, full) == 4600);
    assert(Wire::throttle(1.0f, zero, full) == 4600);
    assert(Wire::throttle(-0.5f, zero, full) == 1000);
    assert(Wire::throttle(std::numeric_limits<float>::quiet_NaN(), zero, full) == 1000);
    assert(Wire::throttle(std::numeric_limits<float>::infinity(), zero, full) == 1000);
    assert(Wire::throttle(1.0f, 2000, 2000) == 0);
    assert(Wire::throttle(1.0f, 1000, 65535) == 0);
}

int main()
{
    no_arming_or_output_without_initial_neutral();
    throttle_is_hard_capped();
    reverse_needs_neutral_confirmed_stop_and_two_dwells();
    cannot_reverse_without_configuration();
    loss_of_any_interlock_latches();
    reject_nan_infinity_and_negative();
    lost_stop_evidence_resets_dwell();
    timestamp_wraparound_is_handled();
    physical_motor_wire_value_is_capped();
    std::puts("9 Bench6 host-only safety-contract tests passed");
}
