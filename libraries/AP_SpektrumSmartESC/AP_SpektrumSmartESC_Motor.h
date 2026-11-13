/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Bus.h"
#include "AP_SpektrumSmartESC_ChannelData.h"
#include "AP_SpektrumSmartESC_Commands.h"
#include "AP_SpektrumSmartESC_Reader.h"
#include "AP_SpektrumSmartESC_Lab.h"
#ifndef AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED
#define AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
#define AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL 0
#endif

// Dedicated-ESC coordinator with a separately gated native backend. Ordinary
// builds cannot transmit. IO readback validates software ownership; physical
// output inhibition and ESC shutdown behavior still require bench evidence.
class AP_SpektrumSmartESC_Motor {
public:
    enum class State : uint8_t {
        DISABLED, DISCOVERY, IDLE, FORWARD, REVERSE_REQUESTED,
        NEUTRAL_TRANSITION, REVERSE_ACTIVE, RETURN_TO_FORWARD, FAULT,
        THROTTLE_TO_IDLE, NEUTRAL_DWELL = NEUTRAL_TRANSITION
    };
    enum class Fault : uint8_t {
        NONE, QUALIFICATION, OWNERSHIP, DISARMED, RC_LOSS, CONTROL_LOSS,
        ESC_LOSS, UART, INVALID_DEMAND, GROUND, REVERSE_DENIED, STOP_EVIDENCE,
        PROTOCOL, TX_TIMEOUT, TURNAROUND
    };
    struct Qualification {
        uint32_t io_firmware_crc = 0; // Explicit reviewed image CRC, never inferred from a capability version.
        bool electrical = false, protocol = false, endpoints = false;
        bool esc_loss_behavior = false, reverse_settings = false;
    };
    struct Receipt {
        uint32_t epoch = 0, io_boot = 0, acquired_ms = 0;
        bool main3_inhibited = false, failsafe_inhibited = false, aux7_reserved = false;
    };
    class Owner {
    public:
        virtual ~Owner() = default;
        virtual bool acquire(uint32_t epoch) = 0;
        virtual Receipt receipt() const = 0;
        // Release UART ownership only. Must NOT automatically re-enable MAIN3.
        virtual void release() = 0;
    };
    struct Demand {
        float magnitude = 0; // 0..1 final vehicle demand; never a raw RC value.
        bool reverse = false; // Explicit authorized request, not sign inference.
        bool armed = false, rc_ok = false, control_ok = false, failsafe = false;
        bool reverse_permitted = false, ground_authorized = false, stopped = false;
        uint32_t acquired_ms = 0, ground_ms = 0, stopped_ms = 0;
    };
    struct Status {
        State state = State::DISABLED;
        Fault fault = Fault::NONE;
        uint32_t tx_frames = 0, tx_bytes = 0, controls = 0, telemetry = 0;
        uint32_t late_sensors = 0; // Valid but outside active 0xCD reply window; never authorizes ARM.
        uint16_t throttle = 0, direction = 0; // Last TC-completed control, not desired values.
        bool simulated = true, reverse = false;
    };
    AP_SpektrumSmartESC_Motor(AP_SpektrumSmartESC_Bus::Port &port, Owner &owner,
                              const AP_SpektrumSmartESC_Commands::Profile &profile);
    ~AP_SpektrumSmartESC_Motor() { disable(); }
    AP_SpektrumSmartESC_Motor(const AP_SpektrumSmartESC_Motor &) = delete;
    AP_SpektrumSmartESC_Motor &operator=(const AP_SpektrumSmartESC_Motor &) = delete;
    bool start(const Qualification &q, bool disarmed, uint8_t target, uint32_t uid, uint32_t now_us, uint32_t now_ms);
    void update(const Demand &d, uint32_t now_us, uint32_t now_ms);
    void feed(uint8_t byte, uint32_t acquisition_us);
    void disable();
    const Status &status() const { return _status; }
    // Worker may need to retire the previous physical UART transaction
    // before draining RX. Only a true hardware TC may clear this flag;
    // it is never permission to queue a new command.
    bool transmitting() const { return _tx; }
    bool telemetry(AP_SpektrumSmartESC::Telemetry &data, uint32_t now_us) const;
    uint32_t telemetry_age_us(uint32_t now_us) const { return uint32_t(now_us - _telemetry_since); }
    bool ready_to_arm(uint32_t now_ms, uint32_t now_us) const
    {
        return _leased && (_status.state == State::IDLE || _status.state == State::FORWARD) &&
            owned(now_ms) && _initial_idle && !_armed_seen &&
            _status.fault == Fault::NONE && _status.controls != 0 &&
            // Another ordinary CH1=zero telemetry poll may already be awaiting
            // a reply when Plane checks ARM readiness. Do not make the ARM
            // gate depend on that narrow bus-slot phase: the previous
            // TC-completed neutral command and an independently fresh,
            // stopped/zero-throttle 0x80 sample remain mandatory. Failure of
            // the pending poll is still handled by the motor watchdog.
            // A handshake-only bench session never qualifies for flight arming.
            !_profile.handshake_only_bench &&
            _status.throttle == _profile.zero && !_status.reverse &&
            // A local UART TC or bare 0x21 heartbeat is not proof the ESC has
            // accepted an idle command. Require a fresh telemetry candidate
            // reporting BOTH zero commanded throttle and stopped electrical RPM
            // on the separately isolated verified single-ESC bus.
            _have_telemetry && _status.telemetry != 0 &&
            (_telemetry.valid & (AP_SpektrumSmartESC::ERPM | AP_SpektrumSmartESC::THROTTLE)) ==
                (AP_SpektrumSmartESC::ERPM | AP_SpektrumSmartESC::THROTTLE) &&
            _telemetry.electrical_rpm == 0 && _telemetry.throttle_pct <= 0.5f &&
            uint32_t(now_us - _telemetry_since) < 50000U;
            // After the initial 0x21 discovery, SRXL2 normal operation is
            // 0xCD / solicited 0x80. No periodic 0x21 heartbeat is required.
    }
    static constexpr uint8_t native_fc_id(bool simulated)
    {
        // Dedicated FC/master bus: 0x30, already observed physically on
        // Pixhawk6C <-> ESC 0x40. Keep legacy 0x31 simulator vectors.
        return simulated ? AP_SpektrumSmartESC_Reader::SOURCE :
            AP_SpektrumSmartESC_Reader::POWERON_SOURCE;
    }
    static constexpr bool physical_control_available()
    {
        return !AP_SPEKTRUM_SMART_ESC_LAB_ONLY &&
            ((AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED &&
              AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED) ||
             // Separate opt-in board: normal arming, 0..6% digital CH1
             // only, reverse physically disabled. NOT production-certified.
             AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL);
    }
    static bool control_packet(const uint8_t *packet, uint8_t length);
private:
    void fault(Fault reason);
    bool owned(uint32_t now_ms) const;
    bool transmit(uint8_t length, uint32_t now_us, bool handshake);
    void control(const Demand &d, uint32_t now_us, uint32_t now_ms);
    AP_SpektrumSmartESC_Bus::Port &_port;
    Owner &_owner;
    AP_SpektrumSmartESC_Commands::Profile _profile;
    Qualification _qualification;
    AP_SpektrumSmartESC_Reader _reader;
    AP_SpektrumSmartESC _parser;
    Status _status;
    uint8_t _packet[80] {};
    uint8_t _length = 0, _target = 0, _source = 0;
    bool _leased = false, _tx = false, _handshake_tx = false, _waiting_reply = false;
    bool _initial_idle = false, _transition = false, _neutral = false, _have_telemetry = false;
    bool _direction_changed = false;
    bool _armed_seen = false;
    uint32_t _epoch = 0, _io_boot = 0, _tx_since = 0, _last_tx = 0, _last_rx = 0;
    uint32_t _reply_since = 0, _neutral_since = 0, _telemetry_since = 0;
    uint32_t _last_peer = 0;
    uint32_t _last_control_start = 0;
    uint32_t _direction_since = 0;
    uint16_t _pending_throttle = 0, _pending_direction = 0;
    AP_SpektrumSmartESC::Telemetry _telemetry {};
};
