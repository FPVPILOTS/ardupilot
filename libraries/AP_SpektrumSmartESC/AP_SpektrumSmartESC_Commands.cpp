/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Commands.h"
#include "AP_SpektrumSmartESC_ChannelData.h"
#include <math.h>

static bool channel_value(uint16_t value)
{
    return value <= 65532U && (value & 3U) == 0;
}

void AP_SpektrumSmartESC_Commands::reset()
{
    _authorized = false;
    _reverse = false;
    _neutral = false;
}

uint8_t AP_SpektrumSmartESC_Commands::prepare(const Demand &d, uint32_t now_ms, uint8_t *packet, size_t capacity)
{
    if (!_profile.throttle_verified || _profile.throttle_channel >= 32 ||
        !channel_value(_profile.zero) || !channel_value(_profile.full) || _profile.zero >= _profile.full ||
        !isfinite(d.throttle) || d.throttle < 0 || d.throttle > 1 ||
        !d.armed || !d.rc_ok || !d.esc_ok || d.failsafe) {
        reset();
        return 0;
    }
    if (_profile.reverse_verified && (_profile.reverse_channel >= 32 ||
        _profile.reverse_channel == _profile.throttle_channel ||
        !channel_value(_profile.forward) || !channel_value(_profile.reverse) ||
        _profile.forward == _profile.reverse || _profile.direction_dwell_ms == 0)) {
        reset();
        return 0;
    }
    if (d.reverse && !_profile.reverse_verified) {
        reset();
        return 0;
    }
    if (packet == nullptr || capacity < size_t(_profile.reverse_verified ? 18 : 16)) {
        return 0;
    }
    // Following startup or any loss, require an explicitly stopped, zero,
    // forward demand before authorizing any nonzero simulated command.
    if (!_authorized) {
        if (d.throttle > 0 || d.reverse || !d.stopped) {
            return 0;
        }
        _authorized = true;
    }
    if (!(d.throttle > 0) && d.stopped) {
        if (!_neutral) {
            _neutral_since_ms = now_ms;
            _neutral = true;
        }
    } else {
        _neutral = false;
    }
    if (d.reverse != _reverse) {
        if (!_neutral || uint32_t(now_ms - _neutral_since_ms) < _profile.direction_dwell_ms) {
            return 0;
        }
        _reverse = d.reverse;
        _neutral_since_ms = now_ms;
    }
    // Rev K channel values occupy bits 15:2. Endpoints and channel assignment
    // must come from independently verified ESC configuration, not this codec.
    const uint32_t span = _profile.full - _profile.zero;
    const uint16_t throttle = uint16_t(_profile.zero + uint32_t(lroundf(d.throttle * (span / 4U))) * 4U);
    const uint32_t mask = (uint32_t(1) << _profile.throttle_channel) |
                          (_profile.reverse_verified ? uint32_t(1) << _profile.reverse_channel : 0);
    uint16_t values[32] {};
    values[_profile.throttle_channel] = throttle;
    if (_profile.reverse_verified) {
        values[_profile.reverse_channel] = _reverse ? _profile.reverse : _profile.forward;
    }
    // Never requests telemetry or claims RF signal strength in this mock API.
    return AP_SpektrumSmartESC_ChannelData::encode(AP_SpektrumSmartESC_ChannelData::Kind::Normal,
        0, 0, 0, mask, values, packet, capacity);
}
