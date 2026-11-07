/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Telem.h"

#if HAL_WITH_ESC_TELEM
#include <math.h>

bool AP_SpektrumSmartESC_Telem::publish(const AP_SpektrumSmartESC &parser, AP_ESC_Telem &frontend,
                                      uint8_t esc_index, uint32_t now_ms)
{
    AP_SpektrumSmartESC::Telemetry source {};
    if (esc_index >= ESC_TELEM_MAX_ESCS || !parser.telemetry(source, now_ms)) {
        return false;
    }
    return publish_data(source, frontend, esc_index);
}

bool AP_SpektrumSmartESC_Telem::publish_data(const AP_SpektrumSmartESC::Telemetry &source,
                                           AP_ESC_Telem &frontend, uint8_t esc_index)
{
    constexpr uint16_t REQUIRED = AP_SpektrumSmartESC::VOLTAGE |
                                  AP_SpektrumSmartESC::CURRENT |
                                  AP_SpektrumSmartESC::TEMPERATURE;
    // The frontend accumulates field validity across updates with a common
    // expiry. Publish only complete snapshots of the fields we own, so missing
    // temperature/current cannot be kept fresh by new voltage-only samples.
    if (esc_index >= ESC_TELEM_MAX_ESCS || (source.valid & REQUIRED) != REQUIRED ||
        !isfinite(source.voltage) || !isfinite(source.current) || !isfinite(source.temperature_c) ||
        source.voltage < 0 || source.current < 0 || source.temperature_c < -327.68f || source.temperature_c > 327.67f) {
        return false;
    }
    AP_ESC_Telem_Backend::TelemetryData data {};
    data.voltage = source.voltage;
    data.current = source.current;
    data.temperature_cdeg = int16_t(lroundf(source.temperature_c * 100.0f));
    frontend.update_telem_data(esc_index, data, AP_ESC_Telem_Backend::VOLTAGE |
                              AP_ESC_Telem_Backend::CURRENT | AP_ESC_Telem_Backend::TEMPERATURE);
    // Electrical RPM cannot be sent as mechanical RPM without a known motor
    // pole count. BEC temperature is not motor temperature. Neither is mapped.
    return true;
}
#endif
