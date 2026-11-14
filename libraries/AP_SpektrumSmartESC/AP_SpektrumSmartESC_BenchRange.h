/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// SRXL2 Rev K §7.7.1 channel-data packet examples, NOT an Avian
// 130A calibration certificate. All values are 16-bit, low two bits zero.
// Example -100% = 0x2AA0 (10912), +100% = 0xD554 (54612).
// Explicit non-flight board: restrict first trial to ~6% of documented
// transmitter range, forward only. Changing this requires new review,
// tests and physical independent motor-stop validation.
struct AP_SpektrumSmartESC_BenchRange {
    static constexpr uint16_t MINUS_100 = 0x2AA0U; // 10912
    static constexpr uint16_t PLUS_100 = 0xD554U;  // 54612
    static constexpr uint16_t ZERO_CANDIDATE = MINUS_100;
    static constexpr uint16_t SIX_PERCENT_CANDIDATE =
        uint16_t(((uint32_t(PLUS_100 - MINUS_100) * 6U) / 100U / 4U) * 4U + ZERO_CANDIDATE);
    static_assert(ZERO_CANDIDATE == 10912U, "SRXL2 low example changed");
    static_assert(SIX_PERCENT_CANDIDATE == 13532U, "SRXL2 6 percent value changed");
    static_assert((SIX_PERCENT_CANDIDATE & 3U) == 0, "SRXL2 reserved bits");
};
