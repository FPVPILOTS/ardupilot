/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"

// Rev K 7.7: wire encoding only. Servo center is NOT an ESC idle calibration.
class AP_SpektrumSmartESC_ChannelData {
public:
    enum class Kind : uint8_t { Normal = 0, Failsafe = 1 };
    static uint8_t encode(Kind kind, uint8_t reply, int8_t rssi, uint16_t losses,
                          uint32_t mask, const uint16_t values[32], uint8_t *packet, size_t capacity);
};
