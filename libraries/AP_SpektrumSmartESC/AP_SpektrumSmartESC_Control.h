/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Commands.h"

// Simulation-only boundary for the final logical controller demand. No HAL,
// vehicle hook, output suppression or physical transport activation is supplied.
// SERVO3/function70 and RC inputs are not SRXL2 channel indexes.
class AP_SpektrumSmartESC_Control {
public:
    enum class Transport : uint8_t { Disabled, Aux7UART4, Main3IOMCU };
    struct Calibration {
        AP_SpektrumSmartESC_Commands::Profile channels;
        bool reverse_settings_verified = false; // Brake Type and actual channel-7 configuration.
    };
    struct Qualification {
        bool electrical_verified = false;
        bool protocol_verified = false;
    };
    struct Ownership {
        uint32_t epoch = 0;
        uint32_t acquired_ms = 0;
        bool main3_pwm_inhibited = false;
        bool aux7_pwm_reserved = false;
        bool exclusive = false;
    };
    struct Demand {
        float throttle_percent = 0; // Final controller demand, -100..100; NOT PWM or RC input.
        bool reverse_request = false; // Allows direction selection at zero magnitude.
        bool reverse_permitted = false; // Vehicle mode/arming restrictions, e.g. Plane allow_reverse_thrust.
        bool armed = false;
        bool rc_ok = false;
        bool esc_ok = false;
        bool failsafe = false;
        bool throttle_suppressed = false;
        bool motor_stopped = false; // Must be real evidence in a future live integration.
    };
    explicit AP_SpektrumSmartESC_Control(const Calibration &calibration);
    bool select_simulated(Transport transport, const Qualification &qualification, bool disarmed);
    uint8_t prepare_simulated(const Demand &demand, const Ownership &ownership,
                              uint32_t now_ms, uint8_t *packet, size_t capacity);
    void disable(); // Never unmutes a PWM output or falls back automatically.
    uint32_t epoch() const { return _epoch; }
    Transport transport() const { return _transport; }
    static constexpr uint8_t THROTTLE_CHANNEL = 0; // SRXL2 channel 1.
    static constexpr uint8_t REVERSE_CHANNEL = 6; // SRXL2 channel 7.
private:
    static AP_SpektrumSmartESC_Commands::Profile profile(const Calibration &calibration);
    void reset_commands();
    AP_SpektrumSmartESC_Commands _commands;
    bool _reverse_configured;
    bool _neutral_seen = false;
    uint32_t _epoch = 0;
    Transport _transport = Transport::Disabled;
};
