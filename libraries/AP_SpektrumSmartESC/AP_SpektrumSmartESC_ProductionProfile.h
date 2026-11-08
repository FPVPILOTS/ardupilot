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
    }
};

inline AP_SpektrumSmartESC_ProductionProfile spektrum_production_profile()
{
    // Source-reviewed values MUST be supplied after independent physical
    // qualification. Deliberately unqualified: a radio/arming request alone
    // is never an approval to drive a 130A ESC or 90mm EDF.
    return {};
}
