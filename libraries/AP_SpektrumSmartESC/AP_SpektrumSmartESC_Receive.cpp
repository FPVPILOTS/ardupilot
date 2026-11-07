/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Receive.h"
#include "AP_SpektrumSmartESC_Telem.h"

bool AP_SpektrumSmartESC_Receive::start()
{
    if (!_port.is_receive_only() || _port.is_initialized()) {
        return false;
    }
    _port.begin(115200);
    _running = _port.is_initialized();
    _errors = _port.receive_errors();
    _parser.reset_observation();
    return _running;
}

void AP_SpektrumSmartESC_Receive::stop()
{
    if (_running) {
        _port.end();
    }
    suspend();
}

bool AP_SpektrumSmartESC_Receive::resume()
{
    AP_HAL::UARTDriver::ReceiveDiagnostics state {};
    if (_port.receive_diagnostics(state) && (state.handshake_flags & 2)) {
        return false; // Cannot resume the passive consumer while the native session owns RX.
    }
    _running = _port.is_receive_only() && _port.is_initialized();
    _errors = _port.receive_errors();
    _parser.reset_observation();
    return _running;
}

void AP_SpektrumSmartESC_Receive::suspend()
{
    _running = false;
    _parser.reset_observation();
#if HAL_WITH_ESC_TELEM
    invalidate();
#endif
}

#if HAL_WITH_ESC_TELEM
bool AP_SpektrumSmartESC_Receive::publish_native(const AP_SpektrumSmartESC::Telemetry &source,
                                               uint32_t sample_ms, uint32_t now_ms)
{
    float rpm;
    if (_running || _port.is_receive_only() || !_port.is_initialized() || _frontend == nullptr ||
        _frontend_conflict) { return false; }
    if (uint32_t(now_ms - sample_ms) >= 50) { invalidate(); return false; }
    if (_frontend->get_telem_data(_index).count != _frontend_count || _frontend->get_rpm(_index,rpm)) {
        _frontend_conflict = true;
        return false;
    }
    if (!AP_SpektrumSmartESC_Telem::publish_data(source,*_frontend,_index)) {
        invalidate();
        return false;
    }
    _published = true;
    _frontend_count = _frontend->get_telem_data(_index).count;
    return true;
}

bool AP_SpektrumSmartESC_Receive::set_frontend(AP_ESC_Telem &frontend, uint8_t index)
{
    float rpm;
    if (index >= ESC_TELEM_MAX_ESCS || frontend.get_last_telem_data_ms(index) != 0 ||
        frontend.get_telem_data(index).types != 0 ||
        frontend.get_rpm(index, rpm)) {
        return false;
    }
    _frontend = &frontend;
    _index = index;
    _frontend_count = frontend.get_telem_data(index).count;
    return true;
}

void AP_SpektrumSmartESC_Receive::invalidate()
{
    if (_frontend != nullptr && _published && !_frontend_conflict) {
        float rpm;
        if (_frontend->get_telem_data(_index).count != _frontend_count || _frontend->get_rpm(_index, rpm)) {
            _frontend_conflict = true;
            return;
        }
        _frontend->invalidate_telem_data(_index);
        _published = false;
    }
}
#endif

void AP_SpektrumSmartESC_Receive::update(uint32_t now_ms)
{
    if (!_running) {
        return;
    }
    if (!_port.is_initialized()) {
        _errors = _port.receive_errors();
        stop();
        return;
    }
#if HAL_WITH_ESC_TELEM
    float rpm;
    if (_frontend != nullptr && (_frontend->get_telem_data(_index).count != _frontend_count ||
                                _frontend->get_rpm(_index, rpm))) {
        // Another producer appeared on the reserved index. Stop touching its
        // data, including invalidation, instead of mixing two sources.
        _frontend_conflict = true;
    }
#endif
    if (_port.receive_errors() != _errors) {
        _errors = _port.receive_errors();
        _parser.reset_observation();
        IGNORE_RETURN(_port.discard_input());
    }
    const uint32_t sequence = _parser.counters().telemetry;
    uint8_t byte;
    uint32_t timestamp;
    for (uint16_t i = 0; i < 512 && _port.read_timed(byte, timestamp); i++) {
        _bytes++;
        _last_byte_ms = timestamp;
        _parser.feed(byte, timestamp);
    }
    if (_port.receive_errors() != _errors) {
        _errors = _port.receive_errors();
        _parser.reset_observation();
        IGNORE_RETURN(_port.discard_input());
    }
    _parser.update(now_ms);
#if HAL_WITH_ESC_TELEM
    if (_frontend != nullptr && !_frontend_conflict) {
        AP_SpektrumSmartESC::Telemetry data {};
        if (!_parser.telemetry(data, now_ms)) {
            invalidate();
        } else if (sequence != _parser.counters().telemetry) {
            // Withdraw the old snapshot even if this new one has missing fields.
            invalidate();
            _published = AP_SpektrumSmartESC_Telem::publish(_parser, *_frontend, _index, now_ms);
            _frontend_count = _frontend->get_telem_data(_index).count;
        }
    }
#endif
}
