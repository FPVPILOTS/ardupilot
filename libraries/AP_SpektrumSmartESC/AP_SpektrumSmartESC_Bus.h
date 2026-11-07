/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#if !defined(SPKTSMART_STANDALONE_TEST) && !defined(SPKTSMART_UART4_REGISTER_TEST) && !defined(SPKTSMART_RX_REGISTER_TEST)
#include "AP_SpektrumSmartESC_config.h"
#endif
#include "AP_SpektrumSmartESC_Handshake.h"

#ifndef AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED
#define AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED 0
#endif

// Testable bidirectional session, deliberately not connected to DiagnosticRX.
// No hardware implementation of this interface is supplied in unverified firmware.
class AP_SpektrumSmartESC_Bus {
public:
    enum class State : uint8_t {
        DISABLED, PASSIVE_RX, BUS_IDLE, HANDSHAKE_PENDING, TX_ACTIVE,
        WAITING_FOR_RESPONSE, CONNECTED, COMMUNICATION_LOST, FAULT
    };
    struct Gate {
        bool electrical_verified = false;
        bool controller_role_verified = false;
        bool esc_initialization_verified = false;
    };
    class Port {
    public:
        virtual ~Port() = default;
        virtual bool acquire() = 0; // Exclusive owner, must not enable TX.
        virtual void relinquish() = 0;
        virtual bool simulated() const = 0;
        virtual bool idle_high() const = 0;
        virtual bool collision() const = 0;
        // Must retain the caller-owned buffer until physical TC, not DMA-empty.
        virtual bool begin_tx(const uint8_t *data, uint8_t length) = 0;
        virtual bool transmission_complete() const = 0;
        virtual bool completion_time_us(uint32_t &) const { return false; }
        virtual bool release_rx() = 0;
    };
    struct Counters {
        uint32_t tx = 0;
        uint32_t crc = 0;
        uint32_t unexpected = 0;
        uint32_t collisions = 0;
        uint32_t timeouts = 0;
        uint32_t telemetry = 0;
    };
    AP_SpektrumSmartESC_Bus(Port &port, const Gate &gate) : _port(port), _gate(gate) {}
    ~AP_SpektrumSmartESC_Bus() { disable(); }
    AP_SpektrumSmartESC_Bus(const AP_SpektrumSmartESC_Bus &) = delete;
    AP_SpektrumSmartESC_Bus &operator=(const AP_SpektrumSmartESC_Bus &) = delete;
    void start(uint32_t now_us, uint32_t uid);
    void disable();
    void activity(uint32_t now_us);
    void update(uint32_t now_us);
    bool observe(const uint8_t *packet, size_t length, uint32_t now_us);
    // Empty-mask polling is only an explicitly requested mock experiment.
    bool request_telemetry(uint32_t now_us, bool empty_mask_semantics_verified);
    bool telemetry(AP_SpektrumSmartESC::Telemetry &result, uint32_t now_us) const;
    // Prepared native diagnostic transport whitelist. Never accepts motor
    // channel values, failsafe channel frames or undocumented commands.
    static bool diagnostic_packet(const uint8_t *packet, size_t length);
    // Deterministic polling experiments only; no autonomous physical poll.
    bool set_mock_poll_interval(uint32_t interval_us, bool empty_mask_semantics_verified);
    State state() const { return _state; }
    const Counters &counters() const { return _counts; }
private:
    enum class Kind : uint8_t { Query, Confirm, Poll };
    void fail(State state);
    bool gated() const;
    void transmit(uint32_t now_us);
    bool queue_poll(uint32_t now_us);
    Port &_port;
    bool _owned = false;
    Gate _gate;
    State _state = State::DISABLED;
    AP_SpektrumSmartESC_Handshake _handshake;
    Counters _counts;
    uint8_t _buffer[AP_SpektrumSmartESC::MAX_PACKET] {};
    uint8_t _length = 0;
    Kind _kind = Kind::Query;
    uint32_t _last_activity = 0;
    uint32_t _phase_since = 0;
    uint32_t _started = 0;
    uint32_t _uid = 0;
    uint32_t _last_peer = 0;
    uint32_t _telemetry_time = 0;
    uint32_t _esc_uid = 0;
    bool _have_telemetry = false;
    uint32_t _poll_interval = 0;
    uint32_t _last_poll = 0;
    AP_SpektrumSmartESC::Telemetry _telemetry {};
};
