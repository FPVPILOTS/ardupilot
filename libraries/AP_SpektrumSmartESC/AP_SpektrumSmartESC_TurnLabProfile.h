/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_TurnLab.h"

// Source-scoped reference candidate for the FIRST step only: sending CH1
// minimum throttle via SRXL2 to see whether the ESC recognizes digital signal.
// Spektrum-compatible receiver mapping in the reviewed WingFlight driver:
// (pulse_us - 988) * 64; 1000us -> 768, 2000us -> 64768.
// These are NOT calibrated on the physical Avian. A no-motor-power attempt
// must precede rotorless verification. Reverse CH7 remains configured on
// the ESC, but the zero-only packet includes CH1 only (mask 0x01).
// No claims of tested electrical behavior, direction polarity, throttle
// acceptance, stop behavior, or qualified 6% motor turn are made here.
inline AP_SpektrumSmartESC_TurnLab::Profile spektrum_turnlab_profile()
{
    AP_SpektrumSmartESC_TurnLab::Profile p;
    p.channels.throttle_channel = 0;       // CH1, no conventional PWM
    p.channels.reverse_channel = 6;        // CH7, not transmitted in zero-only mode
    p.channels.zero = 768;                 // candidate 1000us wire code, NOT proven idle
    p.channels.full = 64768;              // candidate 2000us, never transmitted by zero-only
    p.io_crc = 0x6DBBAD71U;               // verified ON THE OPERATOR'S IOMCU
    // Unqualified physical motion remains impossible:
    // p.channels.throttle_verified = false;
    // p.forward_verified = false;
    // p.electrical = false;
    // p.loss_behavior = false;
    return p;
}
