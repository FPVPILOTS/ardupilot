/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_ChannelData.h"

uint8_t AP_SpektrumSmartESC_ChannelData::encode(Kind kind, uint8_t reply, int8_t rssi, uint16_t losses,
                                               uint32_t mask, const uint16_t values[32],
                                               uint8_t *packet, size_t capacity)
{
    if ((kind != Kind::Normal && kind != Kind::Failsafe) || (mask != 0 && values == nullptr)) {
        return 0;
    }
    uint8_t payload[73] {};
    payload[0] = uint8_t(kind);
    payload[1] = reply;
    payload[2] = uint8_t(rssi);
    payload[3] = losses;
    payload[4] = losses >> 8;
    for (uint8_t i = 0; i < 4; i++) {
        payload[5 + i] = mask >> (i * 8);
    }
    uint8_t n = 9;
    for (uint8_t channel = 0; channel < 32; channel++) {
        if (!(mask & (uint32_t(1) << channel))) {
            continue;
        }
        if (values[channel] > 65532U || (values[channel] & 3U) != 0) {
            return 0;
        }
        payload[n++] = values[channel];
        payload[n++] = values[channel] >> 8;
    }
    return AP_SpektrumSmartESC::encode(0xCD, payload, n, packet, capacity);
}
