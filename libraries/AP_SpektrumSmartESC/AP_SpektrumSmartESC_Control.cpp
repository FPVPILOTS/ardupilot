/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Control.h"
#include <math.h>

AP_SpektrumSmartESC_Commands::Profile AP_SpektrumSmartESC_Control::profile(const Calibration &calibration)
{
    auto result = calibration.channels;
    result.throttle_channel = THROTTLE_CHANNEL;
    result.reverse_channel = REVERSE_CHANNEL;
    result.reverse_verified &= calibration.reverse_settings_verified;
    return result;
}

AP_SpektrumSmartESC_Control::AP_SpektrumSmartESC_Control(const Calibration &calibration) :
    _commands(profile(calibration)),
    _reverse_configured(calibration.channels.reverse_verified && calibration.reverse_settings_verified)
{}

void AP_SpektrumSmartESC_Control::reset_commands()
{
    _commands.reset();
    _neutral_seen = false;
}

void AP_SpektrumSmartESC_Control::disable()
{
    reset_commands();
    _transport = Transport::Disabled;
}

bool AP_SpektrumSmartESC_Control::select_simulated(Transport transport, const Qualification &qualification, bool disarmed)
{
    disable();
    // Fresh epoch invalidates stale output-inhibit acknowledgements, including
    // failed selections. No unsupported MAIN3 timer transport can be selected.
    if (++_epoch == 0) {
        ++_epoch;
    }
    if (!disarmed) {
        return false;
    }
    if (transport == Transport::Disabled) {
        return true;
    }
    if (transport != Transport::Aux7UART4 || !qualification.electrical_verified || !qualification.protocol_verified) {
        return false;
    }
    _transport = transport;
    return true;
}

uint8_t AP_SpektrumSmartESC_Control::prepare_simulated(const Demand &d, const Ownership &owner,
                                                     uint32_t now_ms, uint8_t *packet, size_t capacity)
{
    if (_transport == Transport::Disabled) {
        return 0;
    }
    if (owner.epoch != _epoch || uint32_t(now_ms - owner.acquired_ms) >= 50 ||
        !owner.main3_pwm_inhibited || !owner.aux7_pwm_reserved || !owner.exclusive ||
        !isfinite(d.throttle_percent) || d.throttle_percent < -100 || d.throttle_percent > 100 ||
        !d.rc_ok || !d.esc_ok || d.failsafe) {
        disable();
        return 0;
    }
    if (!d.armed || d.throttle_suppressed) {
        reset_commands();
        return 0;
    }
    const bool reverse = d.reverse_request || d.throttle_percent < 0;
    if (reverse && (!_reverse_configured || !d.reverse_permitted)) {
        disable();
        return 0;
    }
    if (packet == nullptr || capacity < size_t(_reverse_configured ? 18 : 16)) {
        return 0;
    }
    if (!_neutral_seen) {
        if (fabsf(d.throttle_percent) > 0 || reverse || !d.motor_stopped) {
            return 0;
        }
        _neutral_seen = true;
    }
    AP_SpektrumSmartESC_Commands::Demand command;
    command.throttle = fabsf(d.throttle_percent) * 0.01f;
    command.reverse = reverse;
    command.armed = d.armed;
    command.rc_ok = d.rc_ok;
    command.esc_ok = d.esc_ok;
    command.failsafe = d.failsafe;
    command.stopped = d.motor_stopped;
    if (reverse != _commands.direction_reverse()) {
        // First command zero in the current direction; only change channel 7
        // after actual stop evidence and the calibrated dwell. A negative
        // controller demand is not converted to an immediate direction flip.
        command.throttle = 0;
        const uint8_t length = _commands.prepare(command, now_ms, packet, capacity);
        if (length != 0) {
            return length;
        }
        command.reverse = _commands.direction_reverse();
    }
    return _commands.prepare(command, now_ms, packet, capacity);
}
