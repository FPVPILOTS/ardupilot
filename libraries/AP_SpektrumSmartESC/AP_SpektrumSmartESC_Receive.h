/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_ESC_Telem/AP_ESC_Telem.h>

// Diagnostic only. There is deliberately no write call or master state machine
// attached to this class. It requires a backend that guarantees RX-only hardware.
class AP_SpektrumSmartESC_Receive {
public:
    explicit AP_SpektrumSmartESC_Receive(AP_HAL::UARTDriver &port) : _port(port) {}
    bool start();
    void stop();
    void suspend(); // Pause parser/frontend without ending an exclusively leased native port.
    bool resume();
    void update(uint32_t now_ms);
    const AP_SpektrumSmartESC &parser() const { return _parser; }
    uint32_t bytes() const { return _bytes; }
    uint32_t errors() const { return _errors; }
    uint32_t last_byte_ms() const { return _last_byte_ms; }
    bool running() const { return _running; }
#if HAL_WITH_ESC_TELEM
    bool set_frontend(AP_ESC_Telem &frontend, uint8_t index);
    bool frontend_conflict() const { return _frontend_conflict; }
    bool publish_native(const AP_SpektrumSmartESC::Telemetry &source, uint32_t sample_ms, uint32_t now_ms);
    void invalidate_native() { if (!_running) { invalidate(); } }
#endif
private:
    AP_HAL::UARTDriver &_port;
    AP_SpektrumSmartESC _parser {0x30, 0x40, 50, true};
    bool _running = false;
    uint32_t _bytes = 0;
    uint32_t _errors = 0;
    uint32_t _last_byte_ms = 0;
#if HAL_WITH_ESC_TELEM
    void invalidate();
    AP_ESC_Telem *_frontend = nullptr;
    uint8_t _index = 0;
    uint16_t _frontend_count = 0;
    bool _published = false;
    bool _frontend_conflict = false;
#endif
};
