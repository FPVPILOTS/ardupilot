/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Motor.h"

// Actual vehicle motor integration: independent of the one-shot TurnLab.
// The installed special IOMCU CRC is known (0x6DBBAD71) but does NOT
// qualify ESC CH1 endpoints, CH7 direction polarity, electrical isolation,
// or behavior after signal/power loss. Never promote test estimates to
// reviewed physical evidence.
struct AP_SpektrumSmartESC_ProductionProfile {
    AP_SpektrumSmartESC_Commands::Profile channels {};
    AP_SpektrumSmartESC_Motor::Qualification qualification {};
    uint8_t esc_id = 0x40;
    bool operator_qualified = false;

    bool valid() const
    {
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
        // Explicit NON-FLIGHT, separately named build: digital CH1 only,
        // 0..6% of the Spektrum reference full-scale wire interval. These
        // are trial values, not physical full-throttle certification.
        // The operator must still set SPKT_MOTOR=1 and ARM normally,
        // and live TC-correlated zero ESC telemetry is required to arm.
        return operator_qualified && esc_id == 0x40 &&
            qualification.io_firmware_crc == 0x6DBBAD71U &&
            qualification.electrical && qualification.protocol &&
            qualification.endpoints && qualification.esc_loss_behavior &&
            channels.throttle_verified && channels.throttle_channel == 0 &&
            channels.zero == 768U && channels.full == 4608U &&
            !channels.reverse_verified && !qualification.reverse_settings &&
            !channels.handshake_only_bench;
#else
        return operator_qualified &&
            esc_id == 0x40 &&
            qualification.io_firmware_crc == 0x6DBBAD71U &&
            qualification.electrical && qualification.protocol &&
            qualification.endpoints && qualification.esc_loss_behavior &&
            qualification.reverse_settings &&
            channels.throttle_verified && channels.reverse_verified &&
            channels.throttle_channel == 0 && channels.reverse_channel == 6 &&
            !channels.handshake_only_bench &&
            channels.zero < channels.full && channels.full <= 65532U &&
            channels.forward != channels.reverse &&
            channels.forward <= 65532U && channels.reverse <= 65532U &&
            ((channels.zero | channels.full | channels.forward | channels.reverse) & 3U) == 0 &&
            channels.direction_dwell_ms != 0;
#endif
    }
};

inline AP_SpektrumSmartESC_ProductionProfile spektrum_production_profile()
{
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
    // NON-FLIGHT commissioning target exists to COMPILE and TEST the
    // eventual normal-arming integration. A board define, operator intent,
    // or source-derived estimate is NOT hardware qualification.
    // Keep the proposed CH1 wire values for simulation/trace comparison
    // but leave ALL physical capabilities unqualified until reviewed
    // evidence and dedicated hardware tests prove them. A build must not
    // silently grant motor TX/arming simply by selecting this hwdef.
    AP_SpektrumSmartESC_ProductionProfile p;
    p.operator_qualified = false;
    p.esc_id = 0x40;
    p.channels.throttle_verified = false;
    p.channels.throttle_channel = 0;
    p.channels.zero = 768;      // Observed idle candidate, NOT certified.
    p.channels.full = 4608;     // Experimental test maximum, NOT 6% proven RPM.
    p.channels.reverse_verified = false; // CH7 not qualified.
    p.channels.reverse_channel = 6;
    p.qualification.io_firmware_crc = 0x6DBBAD71U;
    p.qualification.electrical = false;
    p.qualification.protocol = false;
    p.qualification.endpoints = false;
    p.qualification.esc_loss_behavior = false;
    p.qualification.reverse_settings = false;
    return p;
#else
    // Default and ordinary flight production builds remain unqualified.
    return {};
#endif
}
