/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Diagnostics.h"
#if AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED && AP_SPEKTRUM_SMART_ESC_TURN_LAB
#include <GCS_MAVLink/GCS.h>
#include <SRV_Channel/SRV_Channel.h>
#include <AP_Logger/AP_Logger.h>
extern const AP_HAL::HAL &hal;

void AP_SpektrumSmartESC_Diagnostics::finish_turn(const char *reason)
{
    const bool completed = _turn.state() == AP_SpektrumSmartESC_TurnLab::State::DONE;
    _turn.abort();
    auto *port = hal.diagnostic_rx_uart();
    if (port != nullptr && _turn_active) {
        (void)port->diagnostic_turn_lab(nullptr, 0, true, 0, 0);
    }
    _turn_owner.release(); // Never restore MAIN3 automatically.
    _turn_pending = _turn_active = _turn_inflight = false;
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 %s Q%lu TC%lu", reason,
                  (unsigned long)_turn.frames(), (unsigned long)_turn.completed());
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 RX%lu WIN%lu OUT%lu; NO STOP PROOF",
                  (unsigned long)_turn_rx, (unsigned long)_turn_reply, (unsigned long)_turn_late);
    if (!completed) {
        GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "SPK22 ABORT: use independent power cutoff");
    }
}

void AP_SpektrumSmartESC_Diagnostics::observe_turn(const uint8_t *packet, uint8_t length, uint32_t ms)
{
    if (!_turn_active) { return; }
    _turn_rx++; // Physical CRC-valid inventory, never simulated or generated TX.
    if (packet[1] == 0x80 && length == 22 && packet[3] == 0x30) {
        // A source-less telemetry candidate is not proof of peer identity or
        // applied throttle. Unsolicited 0x21 frames do not count as telemetry.
        if (_turn_last_tc_ms != 0 && int32_t(ms - _turn_last_tc_ms) >= 0 &&
            uint32_t(ms - _turn_last_tc_ms) <= 12U) {
            _turn_reply++;
        } else {
            _turn_late++;
        }
    }
}

void AP_SpektrumSmartESC_Diagnostics::update_turn(
    uint32_t now, bool safe, const AP_HAL::UARTDriver::ReceiveDiagnostics &hw)
{
    using Turn = AP_SpektrumSmartESC_TurnLab;
    auto *port = hal.diagnostic_rx_uart();
    if (_turnack != 0) {
        (void)_turn_auth.acknowledge(_turnack == 18 ? 15 : 0, safe && !_turn_consumed, now);
        _turnack.set(0);
    }
    if (_turnrun != 0) {
        const int8_t request = _turnrun.get();
        _turnrun.set(0);
        const bool authorized = !_turn_consumed && request == 1 && _turn_auth.consume(safe, now);
        _turn_consumed = true;
        if (!authorized || !spektrum_turnlab_profile().valid()) {
            finish_turn(authorized ? "BLOCKED_PROFILE" : "BLOCKED_AUTH/ABORT");
            return;
        }
        _turn_nonce = _auto_uid ^ now;
        if (_turn_nonce == 0) { _turn_nonce = 1; }
        if (!_turn_owner.prepare(_turn_nonce, spektrum_turnlab_profile().io_crc)) {
            finish_turn("BLOCKED_IO_REQUEST");
            return;
        }
        _turn_pending = true;
        _turn_requested_ms = now;
    }
    if (!_turn_pending && !_turn_active) { return; }
    Turn::Evidence e;
    e.disarmed = !hal.util->get_soft_armed();
    e.emergency_stop = SRV_Channels::get_emergency_stop();
    e.peer = _auto_session.status().state == AP_SpektrumSmartESC_AutoConnect::State::LINKED &&
             _auto_session.status().peer_id == 0x40;
    e.peer_ms = _auto_last_verified_peer_ms;
    e.control_ms = now; // Script supervision, not a stopped/ground sensor claim.
    e.uart_ok = safe && port != nullptr && (_turn_pending ||
        (port->receive_errors() == _turn_error_base && hw.handshake_faults == _turn_fault_base));
    if (_turn_pending) {
        if (!safe || !e.peer || e.peer_ms == 0 || uint32_t(now - e.peer_ms) >= 100 ||
            uint32_t(now - _turn_requested_ms) >= 200) {
            finish_turn("BLOCKED_IO/PEER_TIMEOUT");
            return;
        }
        if (!_turn_owner.acquire(_turn_nonce)) { return; }
    }
    const auto receipt = _turn_owner.receipt();
    e.owner = receipt.epoch == _turn_nonce && receipt.io_boot == _turn_nonce &&
        receipt.main3_inhibited && receipt.failsafe_inhibited && receipt.aux7_reserved &&
        uint32_t(now - receipt.acquired_ms) < 50;
    e.owner_ms = receipt.acquired_ms;
    if (!e.safe(now)) { finish_turn("FAULT_SAFETY"); return; }
    if (_turn_pending) {
        if (_auto_leased || (hw.handshake_flags & 32U) == 0 || (hw.flags & 9U) != 9U) { return; }
        uint32_t started = now;
        if (!port->diagnostic_turn_lab_start(_turn_nonce, started) || !_turn.start(true, e, started)) {
            // May have acquired a native lease even if the coordinator refused.
            _turn_active = true;
            finish_turn("BLOCKED_NATIVE");
            return;
        }
        _turn_pending = false;
        _turn_active = true;
        _turn_error_base = port->receive_errors();
        _turn_fault_base = hw.handshake_faults;
    }
    const bool restored = (hw.flags & 9U) == 9U && (hw.cr1 & 0x4DU) == 5U && (hw.cr3 & 8U);
    if (_turn_inflight && hw.handshake_completed != _turn_tc_base) {
        if (hw.handshake_completed != _turn_tc_base + 1 || !_turn.complete(now, restored)) {
            finish_turn("FAULT_TC/RX"); return;
        }
        _turn_inflight = false;
        _turn_last_tc_ms = hw.handshake_tc_ms;
        if (_turn_logged != _turn.state()) {
            _turn_logged = _turn.state();
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 TC S%u CH1=%u CH7=%u RX%u",
                unsigned(_turn.state()), unsigned(_turn.throttle()),
                unsigned(spektrum_turnlab_profile().channels.forward), unsigned(restored));
        }
#if HAL_LOGGING_ENABLED
        AP::logger().WriteStreaming("SPTN", "TimeUS,Phase,CH1,CH7,Q,TC,Peer,RX,Win,Out", "QBHHIIIIII",
            AP_HAL::micros64(), uint8_t(_turn.state()), _turn.throttle(),
            spektrum_turnlab_profile().channels.forward, _turn.frames(), _turn.completed(),
            e.peer_ms, _turn_rx, _turn_reply, _turn_late);
#endif
    }
    // Inspect deadlines even with an outstanding TX; never reuse or overwrite
    // the buffer until hardware TC restored RX.
    uint8_t preview[Turn::LENGTH] {};
    const uint8_t n = _turn.prepare(e, now, preview, sizeof(preview));
    if (_turn.state() == Turn::State::FAULT || _turn.state() == Turn::State::DONE) {
        finish_turn(_turn.state() == Turn::State::DONE ? "DONE_LATCHED" : "FAULT_DEADLINE"); return;
    }
    if (n != 0 && restored && (hw.handshake_flags & 32U) != 0) {
        for (uint8_t i = 0; i < n; i++) { _turn_packet[i] = preview[i]; }
        if (!_turn.take(_turn_packet, n, e, now) ||
            !port->diagnostic_turn_lab(_turn_packet, n, false, now, e.peer_ms)) {
            finish_turn("FAULT_TX_DENIED"); return;
        }
        _turn_tc_base = hw.handshake_completed;
        _turn_inflight = true;
    }
}
#endif
