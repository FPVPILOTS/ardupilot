/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Bench6.h"
#include <math.h>
#include <stdint.h>

// Physical motor-on path cap; independent of RC input and UI percentages.
// This does NOT qualify an ESC endpoint or enable hardware TX by itself.
// The existing compile-time and electrical gates remain mandatory.
class AP_SpektrumSmartESC_Bench6Wire {
public:
    static float cap(float requested)
    {
        if (!isfinite(requested) || requested <= 0.0f) {
            return 0.0f;
        }
        return requested > AP_SpektrumSmartESC_Bench6::MAX_MAGNITUDE ?
            AP_SpektrumSmartESC_Bench6::MAX_MAGNITUDE : requested;
    }

    static uint16_t throttle(float requested, uint16_t zero, uint16_t full)
    {
        if (zero >= full || full > 65532U || ((zero | full) & 3U) != 0) {
            return 0; // Invalid profile: cannot encode a motor command.
        }
        const uint32_t steps = uint32_t(lroundf(cap(requested) * ((full - zero) / 4U)));
        return uint16_t(zero + 4U * steps);
    }
};
