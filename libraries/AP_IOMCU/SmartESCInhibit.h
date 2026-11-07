/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

// Dedicated IO image permanently removes PB8 from PWM/GPIO. This page is a
// capability/readback, NOT a command that can re-enable that physical output.
// A nonce is reset to zero by every IO boot; old stock IO does not expose it.
struct AP_IOMCU_SmartESCInhibit {
    static constexpr uint16_t VERSION = 1;
    static constexpr uint16_t MAIN3 = 1U << 2;
    uint16_t version = VERSION;
    uint16_t mask = MAIN3;
    uint32_t nonce = 0;
    uint32_t uptime_ms = 0;

    static bool firmware_matches(uint32_t expected, uint32_t observed)
    {
        return expected != 0 && observed == expected;
    }
    bool matches(uint32_t expected_nonce) const
    {
        return version == VERSION && mask == MAIN3 && expected_nonce != 0 && nonce == expected_nonce;
    }
    bool continues(const AP_IOMCU_SmartESCInhibit &previous, uint32_t expected_nonce) const
    {
        // Polls are separated by 20 ms. An unchanged uptime is a replay/frozen
        // response, not fresh evidence, even if UART receipt time advanced.
        return matches(expected_nonce) && int32_t(uptime_ms - previous.uptime_ms) > 0;
    }
};
static_assert(sizeof(AP_IOMCU_SmartESCInhibit) == 12, "IO inhibit page layout");
