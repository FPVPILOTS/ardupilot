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
    // Operator-requested, strictly NON-FLIGHT, forward-only 6% bench
    // commissioning build. These are explicit *trial assumptions*, not
    // assertions of flight qualification, verified loss behavior or
    // independently proven full endpoints. Do not ship as production.
    AP_SpektrumSmartESC_ProductionProfile p;
    p.operator_qualified = true; // This build is deliberate opt-in only.
    p.esc_id = 0x40;
    p.channels.throttle_verified = true; // CH1=768 reported THpct0 in bench.
    p.channels.throttle_channel = 0;
    p.channels.zero = 768;
    p.channels.full = 4608; // Even full stick is only a 6% digital trial.
    p.channels.reverse_verified = false; // CH7 NOT sent; reverse blocked.
    p.channels.reverse_channel = 6;
    p.qualification.io_firmware_crc = 0x6DBBAD71U;
    // Below are BENCH-CANDIDATE assumptions controlled by an independent
    // IOMCU inhibit, limited output profile and watchdogs, not flight proof.
    p.qualification.electrical = true;
    p.qualification.protocol = true;
    p.qualification.endpoints = true;
    p.qualification.esc_loss_behavior = true;
    p.qualification.reverse_settings = false;
    return p;
#else
    // Default and ordinary flight production builds remain unqualified.
    return {};
#endif
}
