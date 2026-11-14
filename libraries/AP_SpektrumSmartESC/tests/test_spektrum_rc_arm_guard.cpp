/* SPDX-License-Identifier: GPL-3.0-or-later */
// Host-only gate for real TX16S RC3, independent of disarmed SERVO3 zero.
// This test does not drive motor, UART, PWM, SRXL2 or IOMCU.
#include "../AP_SpektrumSmartESC_RCArm.h"
#include <cassert>
#include <cstdio>

using Gate = AP_SpektrumSmartESC_RCArm;

int main()
{
    static_assert(Gate::IDLE_MARGIN_US < 100, "Never permit high stick ARM");
    static_assert(Gate::IDLE_HOLD_MS >= 500, "Require stable low stick");
    // Expected configured TX16S throttle, forward and reversed.
    assert(Gate::idle(1000,1000,2000,false));
    assert(Gate::idle(1035,1000,2000,false));
    assert(!Gate::idle(1050,1000,2000,false));
    assert(!Gate::idle(1500,1000,2000,false));
    assert(!Gate::idle(2000,1000,2000,false));
    assert(Gate::idle(2000,1000,2000,true));
    assert(Gate::idle(1960,1000,2000,true));
    assert(!Gate::idle(1900,1000,2000,true));
    assert(!Gate::idle(1000,1000,2000,true));

    // No RX, misconfigured endpoints, implausible/default calibrations.
    assert(!Gate::idle(0,1000,2000,false));
    // Real TX16S observed on 2026-11-14: 992us at idle is
    // acceptable when RC3_MIN is calibrated to 1000us.
    assert(Gate::idle(992,1000,2000,false));
    assert(Gate::idle(990,1000,2000,false));
    assert(!Gate::idle(950,1000,2000,false));
    assert(Gate::idle(2011,1000,2000,true));
    assert(!Gate::idle(2050,1000,2000,true));
    assert(!Gate::idle(2020,1000,2000,false));
    assert(!Gate::idle(1500,1500,1500,false));
    assert(!Gate::idle(1000,1000,1400,false));
    assert(!Gate::idle(1000,500,2000,false));
    assert(!Gate::idle(1000,1000,2400,false));

    // A single fresh minimum sample must never allow ARM.
    assert(!Gate::stable(0,1000,1000));
    assert(!Gate::stable(1000,1000,1000));
    assert(!Gate::stable(1000,1500,1500));
    assert(!Gate::stable(1000,1749,1749));
    assert(Gate::stable(1000,1750,1750));
    assert(Gate::stable(1000,1759,1759));
    assert(!Gate::stable(1000,1740,1840)); // stale input
    assert(!Gate::stable(1000,1750,1900));
    std::puts("EarlyArmedBench real RC3 idle/hold arming tests passed (host only)");
}
