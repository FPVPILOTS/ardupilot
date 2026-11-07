/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Bus.h"

// Offline one-shot experiment. No firmware factory, parameter or vehicle call.
// A future physical adapter still requires the compile-time electrical gate.
class AP_SpektrumSmartESC_HandshakeBench {
public:
    AP_SpektrumSmartESC_HandshakeBench(AP_SpektrumSmartESC_Bus::Port &port,
                                      const AP_SpektrumSmartESC_Bus::Gate &gate) : _bus(port, gate) {}
    bool authorize_once(bool reviewed, bool disarmed, uint32_t now_us, uint32_t uid)
    {
        stop();
        if (!reviewed || !disarmed) {
            return false;
        }
        _bus.start(now_us, uid);
        _active = _bus.state() == AP_SpektrumSmartESC_Bus::State::BUS_IDLE;
        _started = now_us;
        if (!_active) {
            stop();
        }
        return _active;
    }
    void update(bool disarmed, uint32_t now_us)
    {
        if (!_active) {
            return;
        }
        if (!disarmed || uint32_t(now_us - _started) >= 200000) {
            stop();
            return;
        }
        _bus.update(now_us);
        const auto state = _bus.state();
        if (state != AP_SpektrumSmartESC_Bus::State::BUS_IDLE &&
            state != AP_SpektrumSmartESC_Bus::State::HANDSHAKE_PENDING &&
            state != AP_SpektrumSmartESC_Bus::State::TX_ACTIVE &&
            state != AP_SpektrumSmartESC_Bus::State::WAITING_FOR_RESPONSE) {
            stop(); // Includes successful final handshake: no automatic polls.
        }
    }
    bool observe(const uint8_t *packet, size_t length, uint32_t now_us)
    {
        if (!_active) {
            return false;
        }
        if (uint32_t(now_us - _started) >= 200000 || packet == nullptr || length != 14 ||
            packet[1] != 0x21 || !_bus.observe(packet, length, now_us)) {
            stop();
            return false;
        }
        return true;
    }
    void stop() { _active = false; _bus.disable(); }
    bool active() const { return _active; }
    const AP_SpektrumSmartESC_Bus::Counters &counters() const { return _bus.counters(); }
private:
    AP_SpektrumSmartESC_Bus _bus;
    bool _active = false;
    uint32_t _started = 0;
};
