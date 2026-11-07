/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "AP_SpektrumSmartESC.h"
#include <AP_ESC_Telem/AP_ESC_Telem.h>

#if HAL_WITH_ESC_TELEM
// Caller must exclusively own the ESC telemetry index. No UART initialization,
// GPIO configuration, motor commands or vehicle-specific dependencies.
// Call once per newly accepted telemetry frame, not periodically to refresh it.
class AP_SpektrumSmartESC_Telem {
public:
    static bool publish(const AP_SpektrumSmartESC &parser, AP_ESC_Telem &frontend,
                        uint8_t esc_index, uint32_t now_ms);
    static bool publish_data(const AP_SpektrumSmartESC::Telemetry &source, AP_ESC_Telem &frontend,
                             uint8_t esc_index);
};
#endif
