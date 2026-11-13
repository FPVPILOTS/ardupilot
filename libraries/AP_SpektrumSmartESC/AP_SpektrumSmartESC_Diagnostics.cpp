/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_Diagnostics.h"
#if AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
#include <AP_Logger/AP_Logger.h>
#include <GCS_MAVLink/GCS.h>
#include <SRV_Channel/SRV_Channel.h>
#include <RC_Channel/RC_Channel.h>
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB && HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
#endif
#include "AP_SpektrumSmartESC_Shadow.h"
#include "AP_SpektrumSmartESC_ChibiOS.h"
#include "AP_SpektrumSmartESC_ProductionProfile.h"

extern const AP_HAL::HAL &hal;

const AP_Param::GroupInfo AP_SpektrumSmartESC_Diagnostics::var_info[] = {
    // @Param: ENABLE
    // @DisplayName: Spektrum passive UART4 diagnostics
    // @Description: Enable receive-only AUX7 bench diagnostics at 115200 baud and inhibit arming until disabled and rebooted. No transmitter, throttle or reverse. Qualify the electrical path before connecting an ESC or test signal.
    // @Values: 0:Disabled,1:Receive only
    // @RebootRequired: True
    // @User: Advanced
    // Independent SHADOW/PROBE controls must remain visible with legacy RX disabled.
    AP_GROUPINFO("ENABLE", 1, AP_SpektrumSmartESC_Diagnostics, _enable, 0),
    // @Param: TLM
    // @DisplayName: Spektrum observed telemetry publication
    // @Description: Publish fresh complete observed ESC measurements to fixed ESC telemetry index 14 (ESC 15), independently of output mapping. Do not configure another telemetry producer on this index. This is not an arming-health check.
    // @Values: 0:Disabled,1:Enabled
    // @RebootRequired: True
    // @User: Advanced
    AP_GROUPINFO("TLM", 2, AP_SpektrumSmartESC_Diagnostics, _telem, 0),
    // @Param: EXP
    // @DisplayName: Spektrum bench experiment identifier
    // @Description: Nonnegative label recorded with bench diagnostic logs. This label authorizes no electrical operation or transmission. Set a new label for each reviewed measurement experiment.
    // @Range: 0 2147483647
    // @User: Advanced
    AP_GROUPINFO("EXP", 3, AP_SpektrumSmartESC_Diagnostics, _experiment, 0),
    // @Param: SHADOW
    // @DisplayName: Spektrum Plane read-only shadow observer
    // @Description: Log final SERVO3 throttle PWM and inferred signed demand in Plane only. No ownership, packets, transmission or motor control. ESC endpoints and reverse levels remain unqualified. Independent of receive-only bench diagnostics.
    // @Values: 0:Disabled,1:Plane observation
    // @User: Advanced
    // @Vehicles: Plane
    AP_GROUPINFO("SHADOW", 4, AP_SpektrumSmartESC_Diagnostics, _shadow, 0),
    // @Param: PROBE
    // @DisplayName: Spektrum AUX7 disconnected hardware probe
    // @Description: MCU-input-only AUX7 probe, with ESC and external stimuli disconnected. Requires ENABLE=0 and disarmed. Select 0 after boot before requesting a probe; persisted nonzero values cannot start it. Authorization expires after 60 seconds without extension by mode changes. Latches arming inhibition until cleared and rebooted. Internal weak pulls may affect an unknown external board buffer; they do not prove connector voltage or input tolerance.
    // @Values: 0:Disabled,1:Input floating,2:Input weak pull-up,3:Input weak pull-down,4:UART receive only
    // @User: Advanced
    AP_GROUPINFO("PROBE", 5, AP_SpektrumSmartESC_Diagnostics, _probe, 0),
    // @Param: HS
    // @DisplayName: Spektrum single handshake request
    // @Description: Requires ENABLE=1, PROBE=0 and disarmed emergency stop. HSLab requires fresh LABACK. Mode 1 is one 200ms directed experiment; HSLab mode 2 prepares for 2 seconds without TX then polls within a fixed 5-second ESC power-on window. No motor or Control Data packets. Requests are consumed and ignored at boot. Write -1 to abort. Ordinary firmware retains its compile-time electrical gate.
    // @Values: -1:Abort HSLab,0:Idle,1:Single experiment,2:HSLab power-on window
    // @User: Advanced
    AP_GROUPINFO("HS", 6, AP_SpektrumSmartESC_Diagnostics, _handshake, 0),
    // @Param: HSID
    // @DisplayName: Spektrum handshake target candidate
    // @Description: Decimal ESC candidate, default 64 (0x40). Actual reply source must match, with destination FC 49 (0x31) in HS=1 or FC 48 (0x30) in the HSLab power-on mode HS=2. Candidate is fixed during an experiment; no automatic ID scan.
    // @Range: 64 79
    // @User: Advanced
    AP_GROUPINFO("HSID", 7, AP_SpektrumSmartESC_Diagnostics, _hs_target, 64),
    // @Param: WAVE
    // @DisplayName: Disconnected AUX7 laboratory waveform request
    // @Description: Consumed one-shot RAM request. Only the separate LAB firmware can send the fixed non-SRXL2 55 AA 55 AA pattern. ESC and all actuators must be disconnected. Ignored at boot, ordinary firmware always refuses. No handshake, throttle or reverse.
    // @Values: 0:Idle,1:Request one waveform
    // @User: Advanced
    AP_GROUPINFO("WAVE", 8, AP_SpektrumSmartESC_Diagnostics, _waveform, 0),
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    // @Param: LABACK
    // @DisplayName: Experimental handshake acknowledgement
    // @Description: Handshake LAB only. Write 15 to acknowledge unverified electrical waveforms and confirm disconnected motor/actuators, isolated BEC positive and disarmed bench use. Then request HS=1 or HS=2 within 10 seconds. One experiment per reboot. Consumed in RAM, ignored at boot, not electrical qualification. HS=-1 or ENABLE=0 aborts.
    // @Values: 0:No acknowledgement,15:Acknowledge bench uncertainty
    // @User: Advanced
    AP_GROUPINFO("LABACK", 9, AP_SpektrumSmartESC_Diagnostics, _hs_ack, 0),
#endif
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    // @Param: AUTO
    // @DisplayName: Automatic handshake-only laboratory sessions
    // @Description: AutoLab firmware ONLY. On boot with ENABLE=1, automatically discover/reconnect isolated Spektrum ESC on AUX7, using 0x21-only handshake; no Control Data, throttle, reverse or arming. Requires disarmed emergency stop. No other bus master.
    // @Values: 0:Disabled,1:Automatic handshake lab
    // @RebootRequired: True
    // @User: Advanced
    AP_GROUPINFO("AUTO", 10, AP_SpektrumSmartESC_Diagnostics, _auto, 0),
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
    // @Param: POLLACK
    // @DisplayName: One-shot motor-disconnected SRXL2 polling bench consent
    // @Description: PollLab firmware ONLY. Set 15 after physically DISCONNECTING ALL THREE MOTOR PHASE WIRES, disconnecting actuators and checking BEC/signal isolation. Then send POLLRUN=1 within ten seconds. This cannot prove physical isolation or motor safety. Never enable for flight.
    // @Values: 0:No acknowledgement,15:Motor disconnected and bench isolated
    // @User: Advanced
    AP_GROUPINFO("POLLACK", 11, AP_SpektrumSmartESC_Diagnostics, _pollack, 0),
    // @Param: POLLRUN
    // @DisplayName: ONE SRXL2 0xCD reply-ID 0x40 bench trial only
    // @Description: A volatile, consumed command requiring prior fresh POLLACK 15, disarmed emergency stop, linked ESC, and on-board dry-run ready. The exact no-channel-update packet is NOT a motor-stop signal. One attempt per reboot and no automatic restart. MOTOR PHASES MUST BE PHYSICALLY DISCONNECTED.
    // @Values: 0:Idle,1:One isolated test
    // @User: Advanced
    AP_GROUPINFO("POLLRUN", 12, AP_SpektrumSmartESC_Diagnostics, _pollrun, 0),
#endif
#endif
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    // @Param: TURNACK
    // @DisplayName: One-shot forward motor-turn bench acknowledgement
    // @Description: TurnLab only. Write 18 to acknowledge an isolated restrained bench with independent power cutoff. Request TURNRUN=2 for a 3s CH1-minimum-only digital readiness trial with CH7 OMITTED and no nonzero throttle; TURNRUN=1 remains the separately qualified forward 0-6-0 motor trial. One request per reboot; never for flight.
    // @Values: 0:Idle,18:Bench operator ready
    // @User: Advanced
    AP_GROUPINFO("TURNACK", 13, AP_SpektrumSmartESC_Diagnostics, _turnack, 0),
    // @Param: TURNRUN
    // @DisplayName: Single CH1-neutral or qualified 6 percent motor trial
    // @Description: TURNRUN=2 sends ONLY the unverified but standard-derived minimum CH1 value in 16-byte 0xCD frames for 3s (no CH7 or nonzero throttle), to test whether Avian recognizes digital idle. TURNRUN=1 remains blocked until independently qualified forward/stop behavior. Both require fresh TURNACK, live linked peer and IO inhibit; any request consumes this boot's single trial.
    // @Values: -1:Abort,0:Idle,1:Qualified 0-6-0 motor trial,2:CH1 minimum only readiness test
    // @User: Advanced
    AP_GROUPINFO("TURNRUN", 14, AP_SpektrumSmartESC_Diagnostics, _turnrun, 0),
#endif
    // @Param: MOTOR
    // @DisplayName: Source-qualified SRXL2 aircraft motor selection
    // @Description: Explicit opt-in to the native Plane motor controller. Never bypasses bench arming prohibition, hardware TX gates, or reviewed physical ESC/IO qualification. If selected but unqualified, arming remains blocked; there is no PWM fallback. Not a motor-test button.
    // @Values: 0:Disabled,1:Select only when source qualified
    // @RebootRequired: True
    // @User: Advanced
    AP_GROUPINFO("MOTOR", 15, AP_SpektrumSmartESC_Diagnostics, _motor_enable, 0),
    AP_GROUPEND
};

void AP_SpektrumSmartESC_Diagnostics::init()
{
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    _hs_ack.set(0);
    _hs_authorization.cancel();
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
    _pollack.set(0);
    _pollrun.set(0); // Saved commands NEVER cause a 0xCD packet at boot.
    _poll_auth.cancel();
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK20 POLLLAB: MOTOR PHASES MUST BE UNPLUGGED");
#endif
#if !AP_SPEKTRUM_SMART_ESC_TURN_LAB
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15C: HSLAB; NO MOTOR; UART ERROR POLICY UPDATED");
#endif
#endif
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    _turnack.set(0);
    _turnrun.set(0);
    _turn_auth.cancel();
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 TURNLAB; UNARMABLE; PROFILE %s",
                  spektrum_turnlab_profile().valid() ? "QUALIFIED" : "BLOCKED");
#endif
    _waveform.set(0);
    if (AP_SPEKTRUM_SMART_ESC_LAB_ONLY) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKL: LAB ONLY; disconnect ESC and actuators");
    }
    _handshake.set(0); // Volatile command: never honor a saved/startup TX request.
    if (_motor_enable.get() == 1) {
        // Production mode is an explicit, boot-scoped selection, never a
        // fallback from a failed TurnLab or missing receiver. Fail CLOSED:
        // the arming check stays blocked even when the profile is absent.
        _motor_selected = true;
        const auto p = spektrum_production_profile();
        const bool eligible = !AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB &&
            !AP_SPEKTRUM_SMART_ESC_TURN_LAB &&
            AP_SpektrumSmartESC_Motor::physical_control_available() &&
            p.valid() && _enable == 0 && _probe == 0;
        if (!eligible) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL,
                          "SPKT MOTOR BLOCKED: SOURCE/IO/ESC UNQUALIFIED");
            return;
        }
        uint8_t board_id[16] {};
        uint8_t board_len = sizeof(board_id);
        if (!hal.util->get_system_id_unformatted(board_id, board_len) ||
            board_len == 0 || board_len > sizeof(board_id)) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "SPKT MOTOR BLOCKED: NO BOARD UID");
            return;
        }
        uint32_t uid = 2166136261U;
        for (uint8_t i = 0; i < board_len; i++) {
            uid = (uid ^ board_id[i]) * 16777619U;
        }
        const uint32_t nonce = (uid ^ AP_HAL::millis()) | 1U;
        if (!prepare_motor(p.channels, p.qualification, p.esc_id, uid, nonce)) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "SPKT MOTOR BLOCKED: IO/START");
        } else {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKT MOTOR PREPARED; ARM GATES ACTIVE");
        }
        return; // Never start the LAB RX master in native production mode.
    }
    _bench.observe_enable(_enable.get() != 0 || _probe.get() != 0 || _handshake.get() != 0);
    if (bench_blocked() && !hal.util->get_soft_armed()) {
        SRV_Channels::set_emergency_stop(true);
    }
    _probe_policy.request(_probe.get(), _enable.get(), !hal.util->get_soft_armed(), AP_HAL::millis());
    if (_probe != 0 || _enable != 1 || hal.util->get_soft_armed()) {
        return;
    }
    auto *port = hal.diagnostic_rx_uart();
    if (port != nullptr) {
        _receiver = NEW_NOTHROW AP_SpektrumSmartESC_Receive(*port);
    }
    if (_receiver == nullptr || !_receiver->start()) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKT: RX unavailable");
        return;
    }
#if HAL_WITH_ESC_TELEM
    if (_telem == 1 && AP_ESC_Telem::get_singleton() != nullptr &&
        !_receiver->set_frontend(*AP_ESC_Telem::get_singleton(), 14)) {
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKT: telemetry index busy");
    }
#endif
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    if (_auto == 1) {
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK20 PollLab: explicit 0xCD only; MOTOR UNPLUGGED");
#else
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 TURNLAB; no automatic motor commands");
#else
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKT: AUTO handshake TX only; no motor/control");
#endif
#endif
    } else
#endif
    {
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT: bench RX only; arming inhibited; no TX");
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT: passive silence may mean no master polling");
    }
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    if (_auto == 1) {
        uint8_t id[16] {};
        uint8_t length = sizeof(id);
        const bool unique = hal.util->get_system_id_unformatted(id, length) &&
                            length > 0 && length <= sizeof(id);
        if (!unique) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK17: NO_UID; AUTO BLOCKED");
            return;
        }
        uint32_t uid = 2166136261U;
        for (uint8_t i = 0; i < length; ++i) {
            uid = (uid ^ id[i]) * 16777619U;
        }
        _auto_uid = uid;
        _auto_init_ms = AP_HAL::millis();
        _auto_first_tc_ms = 0;
        _auto_fault_gate = 0;
        _auto_session.enable(_auto_init_ms, uint8_t(_hs_target.get()), uid);
#if AP_SPEKTRUM_SMART_ESC_EARLY_APP_BOOT
        _early_app_boot_active = true;
#endif
#if AP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT
        // Compare FC-app startup with handshake scheduling. This is NOT
        // the physical ESC battery-on timestamp, which needs an external
        // instrument to measure.
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK25 FAST INIT%lu L%lu P%lu",
                      (unsigned long)_auto_init_ms,
                      (unsigned long)AP_SpektrumSmartESC_AutoConnect::STARTUP_LISTEN_MS,
                      (unsigned long)AP_SpektrumSmartESC_Reader::PREPARE_MS);
#endif
        _auto_rx = AP_SpektrumSmartESC(0x30, uint8_t(_hs_target.get()), 50, false);
        _auto_started = true;
        _receiver->suspend(); // Native AutoLab owns the sole RX consumer.
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 AUTO LINK; TURNRUN requires qualification");
#else
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK17 AUTOLAB ON; NO MOTOR, NO CONTROL");
#endif
    }
#endif
}

bool AP_SpektrumSmartESC_Diagnostics::update_probe(uint32_t now_ms)
{
    const bool involved = _probe != 0 || _probe_applied != 0;
    const bool allowed = _probe_policy.request(_probe.get(), _enable.get(),
                                               !hal.util->get_soft_armed(), now_ms);
    if (!involved) {
        // Keep reporting until the rate-limited status confirms the final safe mode.
        if (_probe_report_pending && log_probe(now_ms)) {
            _probe_report_pending = false;
        }
        return false; // The original ENABLE=1 receive-only path is unchanged.
    }
    _probe_report_pending = true;
    auto *port = hal.diagnostic_rx_uart();
    const uint8_t desired = allowed ? _probe_policy.active() : 0;
    if (desired != _probe_applied || !allowed) {
        if (_receiver != nullptr) {
            _receiver->stop();
        }
        if (port != nullptr) {
            port->end(); // PA0 input first, UART off, FIFO discarded before switching.
        }
        _probe_applied = 0;
        bool ok = desired == 0;
        if (desired == 4 && port != nullptr) {
            if (_receiver == nullptr) {
                _receiver = NEW_NOTHROW AP_SpektrumSmartESC_Receive(*port);
            }
            ok = _receiver != nullptr && _receiver->start();
        } else if (desired != 0 && port != nullptr) {
            const uint16_t options = desired == 2 ? AP_HAL::UARTDriver::OPTION_PULLUP_RX :
                (desired == 3 ? AP_HAL::UARTDriver::OPTION_PULLDOWN_RX : 0);
            ok = port->set_options(options);
        }
        if (ok) {
            _probe_applied = desired;
        } else {
            _probe_policy.cancel();
            if (port != nullptr) {
                port->end();
            }
        }
    }
    AP_HAL::UARTDriver::ReceiveDiagnostics state {};
    if (_probe_applied != 0 && (port == nullptr || !port->receive_diagnostics(state) ||
                               state.probe_mode != _probe_applied)) {
        _probe_policy.cancel();
        _probe_applied = 0;
        if (_receiver != nullptr) {
            _receiver->stop();
        }
        if (port != nullptr) {
            port->end();
        }
    }
    IGNORE_RETURN(log_probe(now_ms));
    return _probe_applied != 4; // UART-only probes reuse the existing parser/log path.
}

bool AP_SpektrumSmartESC_Diagnostics::log_probe(uint32_t now_ms)
{
    AP_HAL::UARTDriver::ReceiveDiagnostics state {};
    auto *port = hal.diagnostic_rx_uart();
    if (port != nullptr) {
        IGNORE_RETURN(port->receive_diagnostics(state));
    }
    const uint32_t bytes = _receiver != nullptr ? _receiver->bytes() : 0;
    const uint32_t frames = _receiver != nullptr ? _receiver->parser().observation().crc_frames : 0;
    const bool reported = now_ms - _last_probe_message_ms >= 5000;
    if (reported) {
        _last_probe_message_ms = now_ms;
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKP M%u Q%d L%u U%u G%lu", _probe_applied, int(_probe.get()),
                      unsigned((state.flags & 2) != 0), unsigned((state.flags & 1) != 0),
                      (unsigned long)state.release_errors);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKP B%lu F%lu FE%lu; MCU only", (unsigned long)bytes,
                      (unsigned long)frames, (unsigned long)state.framing);
    }
    if (now_ms - _last_probe_log_ms < 1000) {
        return reported;
    }
    _last_probe_log_ms = now_ms;
#if HAL_LOGGING_ENABLED
    AP::logger().WriteStreaming("SPKP", "TimeUS,Exp,Req,Mode,Level,UART,Edges,Seen,Dur,Err,Ext", "QibBBBIBIIB",
        AP_HAL::micros64(), MAX(_experiment.get(), 0), int8_t(_probe.get()), _probe_applied,
        uint8_t((state.flags & 2) != 0), uint8_t((state.flags & 1) != 0), state.probe_transitions,
        state.probe_levels, state.probe_duration_ms, state.release_errors, uint8_t(0));
    AP::logger().WriteStreaming("SPKQ", "TimeUS,Bytes,Frames,FrameErr", "QIII",
                               AP_HAL::micros64(), bytes, frames, state.framing);
#endif
    return reported;
}

#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
void AP_SpektrumSmartESC_Diagnostics::update_auto_reader(uint32_t now_ms)
{
    // Only separately compiled, UNARMABLE AutoLab/PollLab images enter.
    // AutoLab: physical 0x21 handshakes only.
    // PollLab: also exactly one user-authorized, mask=0 0xCD; NO motor
    // channel updates, reverse or flight arming, and motor phases MUST
    // be physically unplugged. Packet mask=0 is NOT a motor-stop command.
    if (!_auto_started) {
        return;
    }
    auto *port = hal.diagnostic_rx_uart();
    const bool safe = port != nullptr && !hal.util->get_soft_armed() &&
                      arming_blocked() && SRV_Channels::get_emergency_stop() &&
                      _enable == 1 && _auto == 1 && _probe == 0 && _handshake == 0 &&
                      (!AP_SPEKTRUM_SMART_ESC_POLL_LAB || _telem == 0);
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
    if (_pollack.get() != 0) {
        (void)_poll_auth.acknowledge(_pollack.get(), safe, now_ms);
        _pollack.set(0); // One-time, RAM-only authorization request.
    }
    if (_pollrun.get() != 0) {
        const bool requested = _pollrun.get() == 1;
        _pollrun.set(0); // NEVER repeat, even if the bus is busy.
        const bool approved = requested && !_poll_consumed &&
            _poll_auth.consume(safe, now_ms);
        _poll_consumed = true; // A failed/unsafe attempt also latches out.
        if (approved) {
            _poll_pending = true;
            _poll_request_ms = now_ms;
        } else {
            _poll_fail++;
        }
    }
#endif
    if (!safe) {
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
        update_turn(now_ms, false, {});
#endif
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
        if (_poll_active) {
            (void)port->diagnostic_poll_lab(nullptr, 0, true);
            _poll_active = false;
            _poll_fail++;
        }
        // Stop the independent receive witness on any unsafe transition.
        _post_poll_diag.finish();
        _poll_pending = false;
#endif
        _auto_telem_observer.invalidate();
        if (_auto_fault_gate == 0) { _auto_fault_gate = 1; } // Unsafe bench precondition
        _auto_session.uart_fault();
        if (port != nullptr && _auto_leased) {
            (void)port->diagnostic_handshake(nullptr, 0, true);
        }
        _auto_inflight = _auto_leased = false;
        return;
    }

    AP_HAL::UARTDriver::ReceiveDiagnostics hw {};
    if (!port->receive_diagnostics(hw)) {
        if (_auto_fault_gate == 0) { _auto_fault_gate = 2; } // Diagnostics unavailable
        _auto_session.uart_fault();
    }
    if (_auto_leased && port->receive_errors() != _auto_uart_error_baseline) {
        // Never clear new framing/noise/overrun evidence just to reconnect.
        if (_auto_fault_gate == 0) { _auto_fault_gate = 3; } // New native UART error
        _auto_session.uart_fault();
    }
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
    if (_poll_active && !_poll_tc && (hw.handshake_flags & 4U)) {
        _poll_tc = true;
        _poll_tc_ms = hw.handshake_tc_ms;
        _post_poll_diag.on_true_tc(_poll_tc_ms);
        _poll_sent++;
    }
#endif
    if (_auto_inflight && (hw.handshake_flags & 4) != 0 &&
        _auto_session.status().state == AP_SpektrumSmartESC_AutoConnect::State::HANDSHAKE) {
        if (_auto_first_tc_ms == 0) {
            _auto_first_tc_ms = hw.handshake_tc_ms; // First native TC after this boot
        }
        if (!_auto_session.sent(hw.handshake_tc_ms * 1000U)) {
            if (_auto_fault_gate == 0) { _auto_fault_gate = 4; } // Reader rejected TC
            _auto_session.uart_fault();
        }
        _auto_inflight = false;
    }

#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    if (_turn_inflight && hw.handshake_completed == _turn_tc_base + 1U) {
        // Observe hardware TC before consuming incoming FIFO data, so only
        // the first qualifying 0x80 for THIS TX can serve as a witness.
        if (_turn_last_tc_ms != hw.handshake_tc_ms) {
            _turn_reply_for_tc = false;
        }
        _turn_last_tc_ms = hw.handshake_tc_ms;
    }
#endif
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    // Calculate the same bounded witness before and after draining RX.
    // A genuine CRC/source-checked 0x21 received after 300ms may refresh
    // identity ONLY when the zero-only reply lease was independently valid.
    const auto zero_reply_hold = [&](const AP_HAL::UARTDriver::ReceiveDiagnostics &snapshot) -> bool {
        if (!_turn_active || !_turn_zero_only || _turn_started_ms == 0 ||
            _turn_last_reply_ms == 0 || _auto_last_verified_peer_ms == 0 ||
            uint32_t(now_ms - _turn_started_ms) >= AP_SpektrumSmartESC_TurnLab::ZERO_WINDOW_MS ||
            uint32_t(now_ms - _turn_last_reply_ms) >= AP_SpektrumSmartESC_TurnLab::ZERO_REPLY_FRESH_MS ||
            uint32_t(now_ms - _auto_last_verified_peer_ms) >=
                AP_SpektrumSmartESC_TurnLab::ZERO_AUTHENTIC_PEER_MAX_MS ||
            _turn_bad_zero_sensor || !safe ||
            (snapshot.flags & 9U) != 9U ||
            (snapshot.cr1 & 0x4DU) != 5U ||
            (snapshot.cr3 & 8U) == 0 ||
            port->receive_errors() != _turn_error_base ||
            snapshot.handshake_faults != _turn_fault_base) {
            return false;
        }
        const auto io = _turn_owner.receipt();
        return io.epoch == _turn_nonce && io.io_boot == _turn_nonce &&
            io.main3_inhibited && io.failsafe_inhibited && io.aux7_reserved &&
            uint32_t(now_ms - io.acquired_ms) < 50;
    };
    const bool zero_hold_before_rx = zero_reply_hold(hw);
#endif
    if (!_auto_inflight) {
        uint8_t byte = 0;
        uint32_t acquired_ms = 0;
        for (uint16_t i = 0; i < 512 && port->read_timed(byte, acquired_ms); i++) {
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
            // Observe raw FIFO data independently of the LINKED parser state.
            // Never acknowledge, re-transmit or accept an additional packet.
            _post_poll_diag.feed(byte, acquired_ms);
#endif
            const auto state = _auto_session.status().state;
            if (state == AP_SpektrumSmartESC_AutoConnect::State::HANDSHAKE) {
                _auto_session.feed(byte, acquired_ms * 1000U);
            } else if (state == AP_SpektrumSmartESC_AutoConnect::State::LINKED) {
                const uint32_t previous = _auto_rx.observation().crc_frames;
                _auto_rx.feed(byte, acquired_ms);
                if (_auto_rx.observation().crc_frames != previous) {
                    uint8_t packet[AP_SpektrumSmartESC::MAX_PACKET] {};
                    uint8_t length = 0;
                    if (_auto_rx.observed_frame(packet, sizeof(packet), length)) {
                        // Phase 17E-A: CRC-valid packet inventory and optional
                        // untrusted 0x80 sensor candidate. No poll, motor control,
                        // AP_ESC_Telem publication or extra UART transmissions.
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
                        observe_turn(packet, length, acquired_ms);
#endif
                        _auto_telem_observer.observe_crc_frame(
                            packet, length, acquired_ms, 0x30);
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
                        if (_poll_active && _poll_tc && !_poll_reply &&
                            packet[1] == 0x80 && length == 22 &&
                            packet[3] == 0x30 &&
                            int32_t(acquired_ms - _poll_tc_ms) >= 0 &&
                            uint32_t(acquired_ms - _poll_tc_ms) <= 12U &&
                            _auto_telem_observer.candidate(_poll_sample, acquired_ms)) {
                            _poll_reply = true;
                            _poll_accepted++;
                        }
#endif
                        if (packet[1] == 0xCD ||
                            (packet[1] == 0x21 && packet[3] >= 0x10 && packet[3] < 0x40)) {
                            // Some other controller/master owns this bus.
                            _auto_session.competing_master();
                        } else {
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
                            if (_auto_session.observe_peer(packet, length, acquired_ms, zero_hold_before_rx)) {
#else
                            if (_auto_session.observe_peer(packet, length, acquired_ms)) {
#endif
                                // Never treat a CRC-valid but source-less 0x80
                                // sensor packet as proof that ESC 0x40 is alive.
                                _auto_last_verified_peer_ms = acquired_ms;
                            }
                        }
                    }
                }
            }
        }
    }

    // Forward the exact CRC-validated unsolicited handshake, not just a
    // boolean or a guessed address. Native UART4 validates it independently
    // before allowing a pre-2s handshake-only transaction.
    uint8_t startup_packet[14] {};
    uint32_t startup_acquired_ms = 0;
    if (_auto_leased && _auto_session.take_startup_announcement(
            startup_packet, sizeof(startup_packet), startup_acquired_ms)) {
        if (port->diagnostic_handshake_startup_observed(
                startup_packet, sizeof(startup_packet), startup_acquired_ms)) {
            _auto_fast_grants++;
        } else {
            _auto_fast_denied++;
            if (_auto_fault_gate == 0) { _auto_fault_gate = 5; } // Startup announce rejected
            _auto_session.uart_fault();
        }
    }

    // Hardware reports USART true TC and receiver restoration separately.
    // Never count a queued packet as successful transmission.
    (void)port->receive_diagnostics(hw);
    const bool rx_restored = (hw.flags & 9U) == 9U &&
                             (hw.cr1 & 0x4DU) == 5U &&
                             (hw.cr3 & 8U) != 0;
    const bool bus_idle = (hw.handshake_flags & 32U) != 0;
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    // The 0x21 timestamp is unchanged by 0x80: only the bounded, isolated
    // zero-only reply witness can defer the standard 300ms policy deadline.
    const auto action = _auto_session.tick(now_ms * 1000U, safe, bus_idle, rx_restored,
                                          zero_reply_hold(hw));
#else
    const auto action = _auto_session.tick(now_ms * 1000U, safe, bus_idle, rx_restored);
#endif
    // Phase 17E-C: dry-run in RAM, one preview per confirmed healthy link.
    // No additional UART operation, no 0xCD output, no motor/ESC channels.
    AP_SpektrumSmartESC_PollDryRun::Evidence dry {};
    dry.now_ms = now_ms;
    dry.last_peer_ms = _auto_last_verified_peer_ms;
    dry.uart_errors = port->receive_errors();
    dry.peer_id = _auto_session.status().peer_id;
    dry.linked = _auto_session.status().state ==
        AP_SpektrumSmartESC_AutoConnect::State::LINKED;
    dry.disarmed = !hal.util->get_soft_armed();
    dry.emergency_stop = SRV_Channels::get_emergency_stop();
    dry.rx_restored = rx_restored;
    dry.bus_idle = bus_idle;
    dry.competing_master = _auto_session.status().state ==
        AP_SpektrumSmartESC_AutoConnect::State::YIELDED;
    (void)_auto_poll_dryrun.update(dry);
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
    update_turn(now_ms, safe, hw);
#endif
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
    if (_poll_active) {
        // Close the physical TX lease at its ORIGINAL 15-ms deadline.
        // A separate read-only witness observes one 50-ms peer heartbeat
        // cycle, stopping at TC+80ms without extending any TX lease.
        const bool bad_link_or_hardware =
            !dry.linked || !dry.disarmed || !dry.emergency_stop ||
            port->receive_errors() != _poll_uart_error_baseline ||
            hw.handshake_faults != _poll_hw_fault_baseline ||
            (_poll_tc && !rx_restored);
        const bool timed_out = AP_SpektrumSmartESC_PollLab::timeout(
            now_ms, _poll_start_ms, _poll_tc, _poll_tc_ms);
        if (bad_link_or_hardware || timed_out) {
            (void)port->diagnostic_poll_lab(nullptr, 0, true);
            if (!_poll_reply) {
                _poll_fail++;
            }
            // No TC or an unsafe close: end observation immediately.
            // A clean poll timeout with verified TC keeps only RX recording
            // active, without holding any TX lease or extending acceptance.
            if (bad_link_or_hardware || !_poll_tc) {
                _post_poll_diag.finish();
            }
            _poll_active = false;
        }
    }
    if (_post_poll_diag.active() && _poll_tc &&
        uint32_t(now_ms - _poll_tc_ms) >= 81U) {
        _post_poll_diag.finish();
    }
    if (_poll_pending) {
        if (uint32_t(now_ms - _poll_request_ms) >= 1000U ||
            !dry.linked || !dry.disarmed || !dry.emergency_stop ||
            dry.peer_id != 0x40 || port->receive_errors() !=
                _auto_uart_error_baseline) {
            _poll_pending = false;
            _poll_fail++;
        } else if (rx_restored && bus_idle &&
                   _auto_poll_dryrun.status().reason ==
                       AP_SpektrumSmartESC_PollDryRun::Reason::ALREADY_PREVIEWED &&
                   _auto_last_verified_peer_ms != 0 &&
                   uint32_t(now_ms - _auto_last_verified_peer_ms) <= 100U) {
            _poll_pending = false;
            const uint8_t n = AP_SpektrumSmartESC_PollLab::encode(
                _poll_packet, sizeof(_poll_packet));
            if (n == 14 && port->diagnostic_poll_lab_start()) {
                _poll_uart_error_baseline = port->receive_errors();
                _poll_hw_fault_baseline = hw.handshake_faults;
                if (port->diagnostic_poll_lab(_poll_packet, n, false)) {
                    _poll_active = true;
                    _poll_tc = _poll_reply = false;
                    _poll_start_ms = now_ms;
                    _post_poll_diag.begin();
                } else {
                    (void)port->diagnostic_poll_lab(nullptr, 0, true);
                    _poll_fail++;
                }
            } else {
                _poll_fail++;
            }
        }
    }
#endif
    if (_auto_session.status().state !=
        AP_SpektrumSmartESC_AutoConnect::State::LINKED) {
        _auto_last_verified_peer_ms = 0;
        _auto_telem_observer.invalidate(); // Never replay ESC data after loss.
    }
    if (action == AP_SpektrumSmartESC_AutoConnect::Action::BEGIN_HANDSHAKE
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
        && !_turn_consumed
#endif
        ) {
        _auto_uart_error_baseline = port->receive_errors();
        if (_auto_leased ||
            !port->diagnostic_handshake_start(uint8_t(_hs_target.get()), _auto_uid, true)) {
            if (_auto_fault_gate == 0) { _auto_fault_gate = 6; } // Native lease denied
            _auto_session.uart_fault();
        } else {
            _auto_leased = true;
            _auto_inflight = false;
            _auto_rx.reset_observation();
        }
    }

    if (_auto_leased &&
        _auto_session.status().state == AP_SpektrumSmartESC_AutoConnect::State::HANDSHAKE &&
        !_auto_inflight) {
        (void)port->receive_diagnostics(hw);
        const uint8_t length = _auto_session.prepare(now_ms * 1000U, _auto_packet,
                                                      sizeof(_auto_packet),
                                                      (hw.handshake_flags & 32U) != 0);
        if (length != 0) {
            if (port->diagnostic_handshake(_auto_packet, length, false)) {
                _auto_inflight = true;
            } else {
                if (_auto_fault_gate == 0) { _auto_fault_gate = 7; } // Native 0x21 TX denied
                _auto_session.uart_fault();
            }
        }
    }

    if (_auto_leased &&
        _auto_session.status().state != AP_SpektrumSmartESC_AutoConnect::State::HANDSHAKE) {
        (void)port->diagnostic_handshake(nullptr, 0, true);
        _auto_leased = _auto_inflight = false;
        _auto_rx.reset_observation();
    }
    // State transitions must not be lost between the 5s snapshots.
    // A source-checked handshake may finish and time out within 300ms.
    // Status text is observation ONLY; no UART, arming or motor action.
    const auto &link = _auto_session.status();
    if (link.state != _auto_logged_state ||
        link.reason != _auto_logged_reason) {
        _auto_logged_state = link.state;
        _auto_logged_reason = link.reason;
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK23 LINK %s",
                      AP_SpektrumSmartESC_AutoConnect::state_name(link.state));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK23 WHY %s TRY%lu OK%lu LOST%lu",
                      AP_SpektrumSmartESC_AutoConnect::reason_name(link.reason),
                      (unsigned long)link.attempts,
                      (unsigned long)link.successful,
                      (unsigned long)link.lost);
        // SPK24 is read-only forensic evidence for the exact 20:21 FC-only
        // reboot case: ESC 0x40->D00 announcements while no D30 targeted
        // reply arrives, followed by a framing-error lockout. Events are
        // reported immediately, not lost in the 5-second SPK17 snapshots.
        const auto &hs = _auto_session.reader_status();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK24 A%lu F%lu L%lu Q%lu R%lu",
                      (unsigned long)hs.startup_announcements,
                      (unsigned long)hs.first_announcement_ms,
                      (unsigned long)hs.last_announcement_ms,
                      (unsigned long)hs.last_targeted_tc_ms,
                      (unsigned long)hs.last_targeted_reply_ms);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK24 UART Q%lu C%lu R%lu F%lu",
                      (unsigned long)hw.handshake_queued,
                      (unsigned long)hw.handshake_completed,
                      (unsigned long)hw.handshake_restored,
                      (unsigned long)hw.handshake_faults);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK24 ERR FE%lu NE%lu O%lu REL%lu",
                      (unsigned long)hw.framing,
                      (unsigned long)hw.noise,
                      (unsigned long)hw.overruns,
                      (unsigned long)hw.release_errors);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK24 TIMES RX%lu TC%lu",
                      (unsigned long)hw.last_uart_rx_ms,
                      (unsigned long)hw.handshake_tc_ms);
    }
    if (now_ms - _auto_log_ms >= 5000U) {
        _auto_log_ms = now_ms;
#if AP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK25 INIT%lu FIRSTTC%lu NOW%lu",
                      (unsigned long)_auto_init_ms,
                      (unsigned long)_auto_first_tc_ms,
                      (unsigned long)now_ms);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK26 G%u RX%lu ERR%lu",
                      unsigned(_auto_fault_gate),
                      (unsigned long)hw.last_uart_rx_ms,
                      (unsigned long)port->receive_errors());
#endif
        const auto &st = _auto_session.status();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 %s", AP_SpektrumSmartESC_AutoConnect::state_name(st.state));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 WHY %s", AP_SpektrumSmartESC_AutoConnect::reason_name(st.reason));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 ID%02x TRY%lu OK%lu LOST%lu",
                      unsigned(st.peer_id), (unsigned long)st.attempts,
                      (unsigned long)st.successful, (unsigned long)st.lost);
        // Diagnose transport independently of the post-link observer:
        // _auto_rx CRC counts stay at zero until a link has succeeded.
        // Q/C/R are lifetime UART4 queued/true-TC/RX-restoration counters.
        const auto &hs = _auto_session.reader_status();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 UART Q%lu C%lu R%lu F%lu",
                      (unsigned long)hw.handshake_queued,
                      (unsigned long)hw.handshake_completed,
                      (unsigned long)hw.handshake_restored,
                      (unsigned long)hw.handshake_faults);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 HS TX%lu RXB%lu CRC%lu RP%u",
                      (unsigned long)hs.tx_frames,
                      (unsigned long)hs.rx_bytes,
                      (unsigned long)hs.rx_frames,
                      unsigned(hs.responses));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 HW E%lu FE%lu L%u OWN%u",
                      (unsigned long)port->receive_errors(),
                      (unsigned long)hw.framing,
                      unsigned((hw.flags & 2U) != 0),
                      unsigned((hw.handshake_flags & 2U) != 0));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 PEER S%02x D%02x T%02x A%lu",
                      unsigned(hs.last_hs_src), unsigned(hs.last_hs_dst),
                      unsigned(hs.type), (unsigned long)hs.startup_announcements);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 PEER PRI%u BAUD%u ECHO%lu UNEXP%lu",
                      unsigned(hs.last_hs_priority), unsigned(hs.last_hs_baud),
                      (unsigned long)hs.echoes, (unsigned long)hs.unexpected);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 FAST OK%lu NO%lu",
                      (unsigned long)_auto_fast_grants,
                      (unsigned long)_auto_fast_denied);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK17 POST RX%lu",
                      (unsigned long)_auto_rx.observation().crc_frames);
        const auto &tl = _auto_telem_observer.status();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK18 FRM H%lu C%lu T%lu O%lu",
                      (unsigned long)tl.handshake_frames,
                      (unsigned long)tl.control_frames,
                      (unsigned long)tl.sensor_frames,
                      (unsigned long)tl.other_frames);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK18 ESC CAND%lu BAD%lu OTHER%lu",
                      (unsigned long)tl.esc_candidates,
                      (unsigned long)tl.rejected_sensors,
                      (unsigned long)tl.other_sensors);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK18 LAST D%02x S%02x SUB%02x",
                      unsigned(tl.last_destination),
                      unsigned(tl.last_sensor_id),
                      unsigned(tl.last_secondary_id));
        const auto &dry_status = _auto_poll_dryrun.status();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK19 DRY R%s P%lu B%lu",
                      AP_SpektrumSmartESC_PollDryRun::reason_name(dry_status.reason),
                      (unsigned long)dry_status.previews,
                      (unsigned long)dry_status.blocked);
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK19 DRY; SPK20 1x0xCD EXPLICIT");
#elif AP_SPEKTRUM_SMART_ESC_TURN_LAB
        // TurnLab can transmit real channel-bearing control frames after a
        // separately qualified profile. Never label this image RX-only.
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 MOTOR PROFILE %u ZERO TRIAL %u",
                      unsigned(spektrum_turnlab_profile().valid()),
                      unsigned(spektrum_turnlab_profile().zero_only_candidate()));
#if HAL_WITH_IO_MCU
        // Report the *observed* installed IOMCU image, not the firmware
        // embedded in the FMU APJ. CRC alone is not a pin-isolation proof.
        uint32_t installed_crc = 0;
        if (AP::iomcu() != nullptr && AP::iomcu()->smart_esc_firmware_crc(installed_crc)) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 IO INSTALLED CRC 0x%08lX",
                          (unsigned long)installed_crc);
        } else {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 IO CRC NOT AVAILABLE");
        }
#endif
#else
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK19 NO TX; 0xCD PREVIEW ONLY");
#endif
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK20 POLL TX%lu RX%lu FAIL%lu",
                      (unsigned long)_poll_sent,
                      (unsigned long)_poll_accepted,
                      (unsigned long)_poll_fail);
        if (_poll_consumed) {
            // 1-ms UART FIFO timestamps; bins are approximate, not scope data.
            const auto &w = _post_poll_diag.snapshot();
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK21 TC%u RX%lu PRE%lu A6%lu",
                          unsigned(w.tc), (unsigned long)w.bytes,
                          (unsigned long)w.pre_tc, (unsigned long)w.syncs);
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK21 B%lu/%lu/%lu/%lu",
                          (unsigned long)w.bins[0], (unsigned long)w.bins[1],
                          (unsigned long)w.bins[2], (unsigned long)w.bins[3]);
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK21 H%lu S%lu L%lu C%lu LEN%lu INC%lu",
                          (unsigned long)w.handshake, (unsigned long)w.sensors,
                          (unsigned long)w.late_sensors, (unsigned long)w.bad_crc,
                          (unsigned long)w.bad_length, (unsigned long)w.incomplete);
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK21 RAW%u %02X%02X%02X%02X%02X%02X%02X%02X",
                          unsigned(w.prefix_len),
                          unsigned(w.prefix[0]), unsigned(w.prefix[1]),
                          unsigned(w.prefix[2]), unsigned(w.prefix[3]),
                          unsigned(w.prefix[4]), unsigned(w.prefix[5]),
                          unsigned(w.prefix[6]), unsigned(w.prefix[7]));
        }
        if (_poll_reply) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK20 LAST FL%04x ERPM%lu",
                          unsigned(_poll_sample.valid),
                          (unsigned long)_poll_sample.electrical_rpm);
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK20 LAST V%.2f I%.2f T%.1f",
                          double(_poll_sample.voltage), double(_poll_sample.current),
                          double(_poll_sample.temperature_c));
        }
#endif
        AP_SpektrumSmartESC::Telemetry candidate {};
        if (st.state == AP_SpektrumSmartESC_AutoConnect::State::LINKED &&
            _auto_telem_observer.candidate(candidate, now_ms)) {
            // Not yet authenticated to an ESC source: diagnostic ONLY.
            // ERPM must not be mislabeled as mechanical shaft RPM.
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK18 UNVERIFIED FL%04x ERPM%lu",
                          unsigned(candidate.valid),
                          (unsigned long)candidate.electrical_rpm);
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK18 OBS V%.2f I%.2f T%.1f",
                          double(candidate.voltage), double(candidate.current),
                          double(candidate.temperature_c));
        }
        if (st.state == AP_SpektrumSmartESC_AutoConnect::State::LINKED) {
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK17 POLLLAB: MOTOR UNPLUGGED; NO THROTTLE");
#elif AP_SPEKTRUM_SMART_ESC_TURN_LAB
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK22 TURNLAB; CH1/CH7 TEST ONLY; Q%lu TC%lu",
                          (unsigned long)_turn.frames(), (unsigned long)_turn.completed());
#else
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK17 NO TLM POLL/THROTTLE; RX OBS ONLY");
#endif
        }
    }
}
#if AP_SPEKTRUM_SMART_ESC_EARLY_APP_BOOT
void AP_SpektrumSmartESC_Diagnostics::update_early_boot(uint32_t now_ms)
{
    // Only the dedicated unarmable 0x21-only commissioning image calls this.
    // Do not run this again from a nested scheduler-delay/GCS callback.
    if (!_early_app_boot_active || !_auto_started || _early_app_boot_pumping ||
        (_early_app_boot_last_ms != 0 && now_ms - _early_app_boot_last_ms < 2U)) {
        return;
    }
    _early_app_boot_pumping = true;
    _early_app_boot_last_ms = now_ms;
    update_auto_reader(now_ms);
    _early_app_boot_pumping = false;
}

void AP_SpektrumSmartESC_Diagnostics::finish_early_boot()
{
    _early_app_boot_active = false;
}
#endif
#endif

bool AP_SpektrumSmartESC_Diagnostics::update_reader(uint32_t now_ms)
{
    auto *port = hal.diagnostic_rx_uart();
    const bool safe = !hal.util->get_soft_armed() && arming_blocked() &&
        SRV_Channels::get_emergency_stop() && _enable == 1 && _probe == 0;
    AP_HAL::UARTDriver::ReceiveDiagnostics hardware {};
    if (port != nullptr) {
        IGNORE_RETURN(port->receive_diagnostics(hardware));
    }
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    if (!safe) {
        _hs_authorization.cancel();
    }
    if (_handshake == -1) {
        _handshake.set(0);
        _hs_ack.set(0);
        _hs_authorization.cancel();
        if (_reader.active() || _hs_lab_session) {
            _reader.fault();
        }
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15: ABORT requested");
    }
    if (_hs_ack != 0) {
        const int8_t value = _hs_ack.get();
        _hs_ack.set(0);
        const bool accepted = _hs_authorization.acknowledge(value, safe && !_reader.active() &&
                              (hardware.handshake_flags & 16) != 0 && (hardware.flags & 1) != 0, now_ms);
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15: %s", accepted ? "ACK accepted; HS=1/2 within 10s" : "BLOCKED_ACK");
    }
    log_handshake_lab(now_ms);
#endif
    if (_handshake != 0) {
        const bool power_on = AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB && _handshake == 2;
        const bool request = _handshake == 1 || power_on;
        _handshake.set(0); // Consume once, without saving EEPROM or auto-retry.
        if (!_reader.active() && (now_ms - _last_hs_request_ms >= 5000 || _last_hs_request_ms == 0)) {
            _hs_power_on = power_on;
            _last_hs_request_ms = now_ms;
            uint8_t id[16], length = sizeof(id);
            uint32_t uid = 2166136261U;
            const bool unique = hal.util->get_system_id_unformatted(id, length) && length > 0 && length <= sizeof(id);
            if (unique) {
                for (uint8_t i = 0; i < length; i++) {
                    uid = (uid ^ id[i]) * 16777619U;
                }
            }
            bool gate = (hardware.handshake_flags & 1) != 0;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
            const bool authorized = _hs_authorization.consume(request && safe && unique &&
                                    (hardware.handshake_flags & 16) != 0, now_ms);
            // Snapshot *before* lease creation. A transient error during lease
            // acquisition or later must not disappear into our baseline.
            const uint32_t prior_uart_errors = port != nullptr ? port->receive_errors() : 0;
            gate = authorized && port != nullptr &&
                   port->diagnostic_handshake_start(uint8_t(_hs_target.get()), uid, _hs_power_on);
            if (gate) {
                _hs_uart_error_baseline = prior_uart_errors;
                if (prior_uart_errors != 0) {
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15C: old FE settled; count=%lu",
                                  (unsigned long)prior_uart_errors);
                }
            }
            _hs_lab_session = gate;
            _hs_window_reported = false;
            if (!gate) {
                const char *reason = !request ? "REQUEST" : !safe ? "BENCH_STATE" : !unique ? "UID" :
                    !_hs_authorization.used() ? "ACK" : !authorized ? "USED" : port == nullptr ? "UART_UNAVAILABLE" :
                    !(hardware.handshake_flags & 64) ? "UART_ERROR" : !(hardware.flags & 1) ? "UART_NOT_READY" :
                    !_hs_power_on && !(hardware.flags & 2) ? "LINE_LOW" : "UART_PREFLIGHT";
                GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15 BLOCKED_%s", reason);
            }
#endif
            IGNORE_RETURN(_reader.start(request && safe && gate && unique, safe,
                                       uint8_t(_hs_target.get()), uid, now_ms * 1000U, _hs_power_on));
            _hs_fault_base = hardware.handshake_faults;
            _hs_logged_bytes = 0;
            GCS_SEND_TEXT(gate ? MAV_SEVERITY_INFO : MAV_SEVERITY_WARNING,
                          "SPKT HS: %s", _reader.active() ? "WAITING; one attempt" : "BLOCKED; no TX");
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
            if (_reader.active() && _hs_power_on) {
                GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15 PREPARED; 2s then 5s window");
            }
#endif
        }
    }
    bool permission = (hardware.handshake_flags & 1) != 0;
    bool session = _hs_leased;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    permission = _hs_lab_session && (hardware.handshake_flags & 16) != 0;
    session |= _hs_lab_session;
#endif
    if (!_reader.active() && !session) {
        log_reader(now_ms);
        return false;
    }
    if (!safe || port == nullptr || !permission) {
        _reader.fault();
    }
    if (_reader.active()) {
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
        if (port->receive_errors() != _hs_uart_error_baseline) {
            // A new framing/overrun/noise/config error is never treated as
            // historical recovery during an authorized transaction.
            _reader.fault();
        }
#endif
        if (_hs_leased && (!port->diagnostic_handshake(nullptr, 0, false) ||
                           hardware.handshake_faults != _hs_fault_base)) {
            _reader.update(now_ms * 1000U);
            if (_reader.active()) {
                _reader.fault();
            }
        }
        if (_hs_inflight && (hardware.handshake_flags & 4)) {
            IGNORE_RETURN(_reader.sent(hardware.handshake_tc_ms * 1000U));
            _hs_inflight = false;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
            if (_reader.status().tx_frames == 1) {
                GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 TX_COMPLETE; UART TC");
                if ((hardware.cr1 & 0x4D) == 5 && (hardware.flags & 9) == 9) {
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 RX_RESTORED; MCU state");
                }
            }
#endif
        }
        // Only native FIFO bytes enter this reader. No synthetic response path.
        uint8_t byte; uint32_t stamp;
        for (uint16_t i = 0; !_hs_inflight && _reader.active() && i < 512 &&
             port->read_timed(byte, stamp); i++) {
            // A response can arrive before the main task observes TC. Keep it
            // buffered until sent() establishes the actual TC timestamp.
            _reader.feed(byte, stamp * 1000U);
        }
        _reader.update(now_ms * 1000U);
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
        if (_hs_power_on && !_hs_window_reported && _reader.active() &&
            _reader.status().state != AP_SpektrumSmartESC_Reader::State::PREPARED) {
            _hs_window_reported = true;
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15 TEST_WINDOW_ACTIVE; connect ESC now");
        }
#endif
        if (!_hs_inflight && _reader.active()) {
            // Low/floating before battery connection is not an ESC power detector.
            // Defer polling until quiet; the native port rechecks before TE.
            IGNORE_RETURN(port->receive_diagnostics(hardware));
            const bool idle = !_hs_power_on || (hardware.handshake_flags & 32) != 0;
            const uint8_t count = _reader.prepare(now_ms * 1000U, _hs_packet, sizeof(_hs_packet), idle);
            if (count != 0) {
                if (port->diagnostic_handshake(_hs_packet, count, false)) {
                    _hs_inflight = _hs_leased = true;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
                    if (_reader.status().tx_frames == 0) {
                        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 TX_STARTED; UART queued");
                    }
#if HAL_LOGGING_ENABLED
                    char hex[64] {};
                    static const char digits[] = "0123456789ABCDEF";
                    for (uint8_t j = 0; j < count; j++) {
                        hex[j*2] = digits[_hs_packet[j] >> 4];
                        hex[j*2+1] = digits[_hs_packet[j] & 15];
                    }
                    AP::logger().WriteStreaming("SPKY", "TimeUS,Type,Dst,CRC,Hex", "QBBBZ", AP_HAL::micros64(),
                        _hs_packet[1], _hs_packet[4], uint8_t(AP_SpektrumSmartESC::crc16(_hs_packet, count) == 0), hex);
#endif
#endif
                    if (_receiver != nullptr) {
                        _receiver->suspend(); // Do not end the leased native UART.
                    }
                } else {
                    _reader.fault();
                }
            }
        }
    }
    const bool consumed = _hs_leased || _reader.active();
    if (!_reader.active()) {
        if (port != nullptr && session) {
            IGNORE_RETURN(port->diagnostic_handshake(nullptr, 0, true));
            if (_receiver != nullptr) {
                IGNORE_RETURN(_receiver->resume());
            }
        }
        _hs_leased = _hs_inflight = false;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
        _hs_lab_session = false;
        AP_HAL::UARTDriver::ReceiveDiagnostics restored {};
        const bool rx = port != nullptr && port->receive_diagnostics(restored) &&
            (restored.flags & 9) == 9 && (restored.cr1 & 0x4D) == 5 && (restored.cr3 & 8) != 0;
        const auto &status = _reader.status();
        const char *result = "ERROR";
        if (rx && status.state == AP_SpektrumSmartESC_Reader::State::DISCOVERED && status.responses == 1) {
            result = "HANDSHAKE_OK";
        } else if (rx && status.state == AP_SpektrumSmartESC_Reader::State::TIMEOUT) {
            result = status.responses != 0 ? "TIMEOUT_FINALIZE" : status.tx_frames == 0 ? "TIMEOUT_NO_TX" :
                status.rx_bytes == 0 ? "TIMEOUT_NO_RX" : "TIMEOUT_NO_PEER";
        }
        GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15 %s; RX_RESTORED=%u", result, unsigned(rx));
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 RX_BYTES=%lu ESC_ID=%02x; AUTH expired",
                      (unsigned long)status.rx_bytes, status.device);
#endif
    }
    log_reader(now_ms);
    return consumed;
}

#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
void AP_SpektrumSmartESC_Diagnostics::log_handshake_lab(uint32_t now_ms)
{
    if (now_ms - _hs_lab_status_ms < 5000) {
        return;
    }
    _hs_lab_status_ms = now_ms;
    auto *port = hal.diagnostic_rx_uart();
    AP_HAL::UARTDriver::ReceiveDiagnostics hardware {};
    const bool known = port != nullptr && port->receive_diagnostics(hardware);
    const bool rx = known && (hardware.flags & 9) == 9 && (hardware.cr1 & 0x4D) == 5;
    const auto &s = _reader.status();
    GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPK15B HSLAB U%u RX%u ACK%u USED%u", unsigned(known && (hardware.flags & 1)),
                  unsigned(rx), unsigned(_hs_authorization.ready(now_ms)), unsigned(_hs_authorization.used()));
    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 TX%lu RXB%lu CRCOK%lu ID%02x", (unsigned long)s.tx_frames,
                  (unsigned long)s.rx_bytes, (unsigned long)s.rx_frames, s.device);
    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 TYPE%02x ECHO%lu TMO%lu BUS%lu", s.type, (unsigned long)s.echoes,
                  (unsigned long)s.timeouts, (unsigned long)hardware.handshake_faults);
    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK15 QUEUED%lu TC%lu RXREST%lu", (unsigned long)hardware.handshake_queued,
                  (unsigned long)hardware.handshake_completed, (unsigned long)hardware.handshake_restored);
}
#endif

void AP_SpektrumSmartESC_Diagnostics::log_reader(uint32_t now_ms)
{
    const auto &s = _reader.status();
    if (s.state == AP_SpektrumSmartESC_Reader::State::DISABLED || now_ms - _last_reader_log_ms < 1000) {
        return;
    }
    _last_reader_log_ms = now_ms;
    const char *state = "ACTIVE";
    switch (s.state) {
    case AP_SpektrumSmartESC_Reader::State::BLOCKED: state = "BLOCKED"; break;
    case AP_SpektrumSmartESC_Reader::State::DISCOVERED: state = "DISCOVERED"; break;
    case AP_SpektrumSmartESC_Reader::State::TIMEOUT: state = "TIMEOUT"; break;
    case AP_SpektrumSmartESC_Reader::State::FAULT: state = "FAULT"; break;
    case AP_SpektrumSmartESC_Reader::State::WAITING: state = "WAITING"; break;
    case AP_SpektrumSmartESC_Reader::State::PREPARED: state = "PREPARED"; break;
    default: break;
    }
    AP_HAL::UARTDriver::ReceiveDiagnostics hardware {};
    auto *port = hal.diagnostic_rx_uart();
    const bool have_hardware = port != nullptr && port->receive_diagnostics(hardware);
    if ((now_ms / 1000U) % 5U == 0) {
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT LINK %s ID%02x RX%lu TX%lu", state, s.device,
                      (unsigned long)s.rx_frames, (unsigned long)s.tx_frames);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT PEER%u ECHO%lu CRCERR%lu", unsigned(s.responses),
                      (unsigned long)s.echoes, (unsigned long)s.crc_errors);
        if (have_hardware) {
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT UART FE%lu ERR%lu", (unsigned long)hardware.framing,
                          (unsigned long)port->receive_errors());
        }
    }
#if HAL_LOGGING_ENABLED
    AP::logger().WriteStreaming("SPKM", "TimeUS,St,ID,RXB,RXF,CRC,TXF,TXB,Ec,Tmo,Bad,RxT,TxT", "QBBIIIIIIIIBB",
        AP_HAL::micros64(), uint8_t(s.state), s.device, s.rx_bytes, s.rx_frames, s.crc_errors,
        s.tx_frames, s.tx_bytes, s.echoes, s.timeouts, s.unexpected, s.type,
        uint8_t(s.tx_frames != 0 ? 0x21 : 0));
    AP::logger().WriteStreaming("SPKE", "TimeUS,Peer,Stale", "QBI",
                               AP_HAL::micros64(), s.responses, s.stale_bytes);
    uint8_t raw[64];
    const uint8_t captured = _reader.capture(raw, sizeof(raw));
    while (_hs_logged_bytes < captured) {
        const uint8_t start = _hs_logged_bytes;
        char hex[64] {};
        static const char digits[] = "0123456789ABCDEF";
        uint8_t count = 0;
        while (_hs_logged_bytes < captured && count < 16) {
            const uint8_t byte = raw[_hs_logged_bytes++];
            hex[count * 2] = digits[byte >> 4];
            hex[count * 2 + 1] = digits[byte & 15U];
            count++;
        }
        // At most 64 bytes per attempt. TimeUS is logging time, not wire timing.
        AP::logger().WriteStreaming("SPKX", "TimeUS,Exp,Offset,Len,Hex", "QiBBZ",
                                   AP_HAL::micros64(), MAX(_experiment.get(), 0), start, count, hex);
    }
    if (have_hardware) {
        AP::logger().WriteStreaming("SPKU", "TimeUS,Queued,TC,RXRest,CR1,CR3,ISR", "QIIIIII", AP_HAL::micros64(),
            hardware.handshake_queued, hardware.handshake_completed, hardware.handshake_restored,
            hardware.cr1, hardware.cr3, hardware.isr);
        AP::logger().WriteStreaming("SPKW", "TimeUS,Gate,Flags,TCms,Fault,FE,Pri,Mask,Baud,UID", "QBBIIIBBII",
            AP_HAL::micros64(), uint8_t((hardware.handshake_flags & 1) != 0), hardware.handshake_flags,
            hardware.handshake_tc_ms, hardware.handshake_faults, hardware.framing,
            s.priority, s.baud_mask, s.negotiated_baud, s.uid);
    }
#endif
}

bool AP_SpektrumSmartESC_Diagnostics::update_waveform(uint32_t now_ms)
{
    const bool request = _waveform.get() != 0;
    const int8_t command = _waveform.get();
    _waveform.set(0); // No persistent or boot-time transmission authorization.
    if (!AP_SPEKTRUM_SMART_ESC_LAB_ONLY) {
        if (request) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKL: BLOCKED; ordinary firmware");
        }
        return false;
    }
    auto *port = hal.diagnostic_rx_uart();
    const bool permitted = !hal.util->get_soft_armed() && arming_blocked() &&
        SRV_Channels::get_emergency_stop() && _enable == 1 && _probe == 0 && _handshake == 0;
    if (!_wave_active && request) {
        if (!permitted || command != 1 || port == nullptr ||
            (_wave_requested && uint32_t(now_ms - _wave_last_ms) < 5000)) {
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKL: request denied; no TX");
            return true;
        }
        _wave_requested = true;
        _wave_last_ms = _wave_start_ms = now_ms;
        if (_receiver != nullptr) {
            _receiver->suspend();
        }
        _wave_active = port->diagnostic_waveform(true, false);
        if (!_wave_active) {
            port->diagnostic_waveform(false, true);
            if (_receiver != nullptr) { _receiver->resume(); }
            GCS_SEND_TEXT(MAV_SEVERITY_WARNING, "SPKL: acquisition denied; RX restored");
            return true;
        }
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKL: one 55 AA 55 AA waveform requested");
        return true;
    }
    if (!_wave_active) {
        return false;
    }
    AP_HAL::UARTDriver::ReceiveDiagnostics state {};
    const bool healthy = port != nullptr && port->diagnostic_waveform(false, false) &&
        port->receive_diagnostics(state);
    const bool complete = healthy && (state.handshake_flags & 4) != 0;
    if (!permitted || !healthy || complete || request || uint32_t(now_ms - _wave_start_ms) >= 200) {
        if (port != nullptr) {
            port->diagnostic_waveform(false, true);
        }
        _wave_active = false;
        if (_receiver != nullptr && !hal.util->get_soft_armed()) { _receiver->resume(); }
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKL: %s; RX restored", complete ? "TC observed" : "aborted/timeout");
    }
    return true;
}

void AP_SpektrumSmartESC_Diagnostics::update()
{
    _bench.observe_enable(_enable.get() != 0 || _probe.get() != 0 || _handshake.get() != 0);
    if (bench_blocked() && !hal.util->get_soft_armed()) {
        // Keep existing motor-function suppression latched during bench work,
        // including after ENABLE is cleared pending the required reboot.
        SRV_Channels::set_emergency_stop(true);
    }
    const uint32_t now = AP_HAL::millis();
    if (_native_motor != nullptr || _motor_selected) {
        // Source-qualified native motor selection owns this path.  Previously
        // update() returned without publishing ANY native motor diagnostics,
        // so the operator could not distinguish handshake, zero-throttle
        // commissioning, telemetry loss or a reverse-direction interlock.
        // Read-only reporting: never triggers start, arming or a 0xCD frame.
        if (uint32_t(now - _last_log_ms) >= 1000U) {
            _last_log_ms = now;
            if (_native_motor != nullptr) {
                const auto s = _native_motor->status();
                GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                    "SPK27 M S%u F%u TX%lu CD%lu TLM%lu",
                    unsigned(s.state), unsigned(s.fault),
                    (unsigned long)s.tx_frames,
                    (unsigned long)s.controls,
                    (unsigned long)s.telemetry);
                GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                    "SPK28 ARM MON%u RET%u RDY%u",
                    unsigned(s.bench_monitor), unsigned(s.bench_link_retries),
                    unsigned(motor_arming_ready()));
                GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                    "SPK27 SENSOR LATE%lu",
                    (unsigned long)s.late_sensors);
                GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                    "SPK27 CH1%u CH7%u REV%u RDY%u",
                    unsigned(s.throttle), unsigned(s.direction),
                    unsigned(s.reverse), unsigned(motor_arming_ready()));
                // Physical 2026-11-13 evidence: repeated 0x21 UART true
                // TC (TX92) but never a native discovery/CH1 frame (CD0).
                // Every 5s distinguish *no RX bytes at all* from valid but
                // non-addressed 0x21 announcements, CRC failures and echoes.
                // Counters are read-only and never relax an ARM interlock.
                if (uint32_t(now - _last_reader_log_ms) >= 5000U) {
                    _last_reader_log_ms = now;
                    const auto hs = _native_motor->handshake_status();
                    const auto startup = _native_motor->startup_diagnostics();
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                        "SPK27 RET FE%u P%u Q%u ARM%u",
                        unsigned(startup.fe_attempts),
                        unsigned(startup.retry_pending),
                        unsigned(startup.waiting_for_fe_quiet),
                        unsigned(startup.ever_armed));
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                        "SPK29 HS ST%u Q%lu FIRST%lums",
                        unsigned(hs.state),
                        (unsigned long)hs.tx_frames,
                        (unsigned long)hs.first_targeted_tc_ms);
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                        "SPK27 HS B%lu F%lu RP%u E%lu",
                        (unsigned long)hs.rx_bytes,
                        (unsigned long)hs.rx_frames,
                        unsigned(hs.responses),
                        (unsigned long)hs.echoes);
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                        "SPK27 HS AN%lu BAD%lu CRC%lu",
                        (unsigned long)hs.startup_announcements,
                        (unsigned long)hs.unexpected,
                        (unsigned long)hs.crc_errors);
                    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                        "SPK27 HS S%02X D%02X P%u T%02X",
                        unsigned(hs.last_hs_src),
                        unsigned(hs.last_hs_dst),
                        unsigned(hs.last_hs_priority),
                        unsigned(hs.type));
                    auto *port = hal.diagnostic_rx_uart();
                    AP_HAL::UARTDriver::ReceiveDiagnostics hw {};
                    if (port != nullptr && port->receive_diagnostics(hw)) {
                        GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                            "SPK27 U FL%u RX%lu ER%lu",
                            unsigned(hw.flags),
                            (unsigned long)hw.last_uart_rx_ms,
                            (unsigned long)port->receive_errors());
                        // Raw receive state distinguishes a physically
                        // silent RX pad from a UART stuck in the wrong mode.
                        // Read-only: no UART reset or automatic ARM.
                        GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                            "SPK29 UART C1%08lX C3%08lX IS%08lX",
                            (unsigned long)hw.cr1,
                            (unsigned long)hw.cr3,
                            (unsigned long)hw.isr);
                        GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                            "SPK27 U FE%lu NE%lu ORE%lu",
                            (unsigned long)hw.framing,
                            (unsigned long)hw.noise,
                            (unsigned long)hw.overruns);
                    }
                }
                // A pre-first-TC TURNAROUND can otherwise produce only
                // "F14 TX0", hiding exactly why the native PA0/UART4
                // instance stopped. Read hardware only through the
                // existing guarded diagnostics (no TX, no ARM override).
                if (s.fault == AP_SpektrumSmartESC_Motor::Fault::TURNAROUND) {
                    auto *uart4 = hal.diagnostic_rx_uart();
                    AP_HAL::UARTDriver::ReceiveDiagnostics hw {};
                    if (uart4 != nullptr && uart4->receive_diagnostics(hw)) {
                        GCS_SEND_TEXT(MAV_SEVERITY_WARNING,
                            "SPK27 U FL%u REL%lu FE%lu",
                            unsigned(hw.flags),
                            (unsigned long)hw.release_errors,
                            (unsigned long)hw.framing);
                        GCS_SEND_TEXT(MAV_SEVERITY_WARNING,
                            "SPK27 U CR1%08lX CR3%08lX",
                            (unsigned long)hw.cr1,
                            (unsigned long)hw.cr3);
                    }
                }
            } else {
                GCS_SEND_TEXT(MAV_SEVERITY_WARNING,
                    "SPK27 MOTOR BLOCKED: NO QUALIFIED SESSION");
            }
        }
        return; // Never fall back to bench RX/AutoLab while MOTOR selected.
    }
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    if (_auto == 1
#if AP_SPEKTRUM_SMART_ESC_TURN_LAB
        || _turn_active || _turn_pending
#endif
        ) {
        update_auto_reader(now);
        return;
    }
#endif
    if (update_waveform(now)) {
        return;
    }
    if (update_reader(now)) {
        return;
    }
    if (update_probe(now)) {
        return;
    }
    if ((_enable != 1 && _probe_applied != 4) || hal.util->get_soft_armed()) {
        if (_receiver != nullptr) {
            _receiver->stop();
        }
        _bench.update_silence(AP_HAL::millis(), 0, 0, false);
        return;
    }
    if (_receiver != nullptr) {
        _receiver->update(now);
    }
    _bench.update_silence(now, _receiver != nullptr ? _receiver->bytes() : 0,
                          _receiver != nullptr ? _receiver->last_byte_ms() : 0,
                          _receiver != nullptr && _receiver->running());
    const bool log_due = now - _last_log_ms >= 1000;
    if (log_due) {
        _last_log_ms = now;
#if HAL_LOGGING_ENABLED
        AP_HAL::UARTDriver::ReceiveDiagnostics status;
        auto *port = hal.diagnostic_rx_uart();
        if (port != nullptr && port->receive_diagnostics(status)) {
            AP::logger().WriteStreaming("SPKH", "TimeUS,Exp,Flg,CR1,CR3,ISR,FE,NE,PE,ORE,OVF,RelErr,Tmo",
                                       "QiBIIIIIIIIII", AP_HAL::micros64(), MAX(_experiment.get(), 0),
                                       status.flags, status.cr1, status.cr3, status.isr, status.framing,
                                       status.noise, status.parity, status.overruns, status.overflow,
                                       status.release_errors, _bench.timeouts());
        }
#endif
    }
    if (_receiver == nullptr) {
        return;
    }
    AP_SpektrumSmartESC::Device device {};
    AP_SpektrumSmartESC::Telemetry telemetry {};
    const auto &parser = _receiver->parser();
    uint8_t status = _receiver->running() ? 1 : 0;
    if (parser.device(device, now)) {
        status = 2;
    }
    if (parser.telemetry(telemetry, now)) {
        status = 3;
    }
#if HAL_WITH_ESC_TELEM
    if (_receiver->frontend_conflict()) {
        status = 4;
    }
#endif
    const auto &observation = parser.observation();
    if (now - _last_message_ms >= 5000) {
        _last_message_ms = now;
        const auto &counts = parser.counters();
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT RX%u ID%02x B%lu F%lu", status, observation.handshake_source,
                      (unsigned long)_receiver->bytes(), (unsigned long)observation.crc_frames);
        // Mission Planner-only observability: no DataFlash log needed.
        // RAW counters mean CRC-valid envelope only, NOT accepted telemetry.
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK16 RAW H%lu C%lu E%lu O%lu",
                      (unsigned long)observation.handshake_frames,
                      (unsigned long)observation.control_frames,
                      (unsigned long)observation.sensor_frames,
                      (unsigned long)observation.other_frames);
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPK16 PAR H%lu P%lu T%lu R%lu",
                      (unsigned long)counts.handshakes,
                      (unsigned long)counts.polls,
                      (unsigned long)counts.telemetry,
                      (unsigned long)counts.rejected);
        AP_HAL::UARTDriver::ReceiveDiagnostics hardware {};
        auto *port = hal.diagnostic_rx_uart();
        if (port != nullptr && port->receive_diagnostics(hardware)) {
            GCS_SEND_TEXT(MAV_SEVERITY_INFO, "SPKT U%u L%u C%lu FE%lu T%02x S%02x",
                          unsigned((hardware.flags & 1) != 0), unsigned((hardware.flags & 2) != 0),
                          (unsigned long)counts.crc_errors, (unsigned long)hardware.framing,
                          observation.type, observation.sensor);
        }
    }
    if (!log_due) {
        return;
    }
#if HAL_LOGGING_ENABLED
    AP::logger().WriteStreaming("SPKR", "TimeUS,Exp,Frames,Accepted,LastMs,Type,Sensor,HSID", "QiIIIBBB",
                               AP_HAL::micros64(), MAX(_experiment.get(), 0), observation.crc_frames,
                               observation.accepted, observation.last_ms, observation.type,
                               observation.sensor, observation.handshake_source);
    const auto &counts = parser.counters();
    AP::logger().WriteStreaming("SPKT", "TimeUS,St,ID,UID,RXB,HWE,CRC,Len,Part,Rej,Unk,HS,Poll,Tlm,LRX",
                               "QBBIIIIIIIIIIII", AP_HAL::micros64(), status, device.id, device.uid,
                               _receiver->bytes(), _receiver->errors(), counts.crc_errors, counts.length_errors,
                               counts.incomplete, counts.rejected, counts.unsupported, counts.handshakes,
                               counts.polls, counts.telemetry, _receiver->last_byte_ms());
#endif
}
#endif

#if AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
bool AP_SpektrumSmartESC_Diagnostics::armed_bench_arm_permitted() const
{
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL && \
    CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
    return _motor_enable.get() == 1 && _motor_selected && !bench_blocked() &&
        _native_motor != nullptr && _native_motor->bench_arm_permitted();
#else
    return false;
#endif
}

bool AP_SpektrumSmartESC_Diagnostics::motor_arming_ready() const
{
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
    return AP_SpektrumSmartESC_Motor::physical_control_available() && _native_motor != nullptr &&
        _native_motor->ready_to_arm();
#else
    return false;
#endif
}

bool AP_SpektrumSmartESC_Diagnostics::prepare_motor(const AP_SpektrumSmartESC_Commands::Profile &profile,
    const AP_SpektrumSmartESC_Motor::Qualification &qualification, uint8_t target, uint32_t uid, uint32_t nonce)
{
    _motor_selected = true; // Explicit API selection latches arming protection even on failure.
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
    if (!AP_SpektrumSmartESC_Motor::physical_control_available() || !hal.scheduler->in_main_thread() || hal.util->get_soft_armed() ||
        _native_motor != nullptr || _enable != 0 || _probe != 0 || _handshake != 0) {
        return false;
    }
    auto *uart = static_cast<DiagnosticRX *>(hal.diagnostic_rx_uart());
    if (uart == nullptr) { return false; }
    if (_receiver == nullptr) {
        _receiver = NEW_NOTHROW AP_SpektrumSmartESC_Receive(*uart);
        if (_receiver == nullptr || !_receiver->start()) { return false; }
    }
#if HAL_WITH_ESC_TELEM
    if (_telem == 1 && (AP_ESC_Telem::get_singleton() == nullptr ||
        !_receiver->set_frontend(*AP_ESC_Telem::get_singleton(),14))) { return false; }
#endif
    _native_motor = NEW_NOTHROW AP_SpektrumSmartESC_ChibiOS(*uart, *_receiver, profile);
    if (_native_motor == nullptr) { return false; }
    if (!_native_motor->start(qualification,target,uid,nonce,AP_HAL::millis())) {
        delete _native_motor;
        _native_motor = nullptr;
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AP_SpektrumSmartESC_Diagnostics::motor_evidence(const MotorEvidence &evidence)
{
    if (!AP_SpektrumSmartESC_Motor::physical_control_available() || !hal.scheduler->in_main_thread() || _native_motor == nullptr) {
        return false;
    }
    _motor_evidence = evidence;
    return true;
}

bool AP_SpektrumSmartESC_Diagnostics::esc_reports_stopped() const
{
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
    return AP_SpektrumSmartESC_Motor::physical_control_available() &&
           _native_motor != nullptr && _native_motor->stopped_observed();
#else
    return false;
#endif
}

void AP_SpektrumSmartESC_Diagnostics::plane_control(bool armed, bool rc_ok, bool failsafe,
    bool reverse_available, bool reverse_permitted, bool reverse_configured, bool reverse_selected)
{
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
    if (!AP_SpektrumSmartESC_Motor::physical_control_available() || _native_motor == nullptr) {
        return;
    }
    AP_SpektrumSmartESC_Plane::Snapshot s;
    const auto *channel = SRV_Channels::srv_channel(2);
    s.servo_index = 2;
    if (channel != nullptr) {
        s.function = uint16_t(channel->get_function()); s.pwm = channel->get_output_pwm();
        s.minimum = channel->get_output_min(); s.maximum = channel->get_output_max();
        s.trim = channel->get_trim(); s.reversed = channel->get_reversed();
    }
    s.bipolar = reverse_available; s.armed = armed; s.rc_ok = rc_ok;
    s.control_ok = !failsafe;
    // ArduPilot's emergency stop/safety switch blocks ARMING and any
    // ARMED motor demand. During DISARMED boot the dedicated ESC needs
    // CH1=minimum control for its own idle/zero telemetry; treating the
    // engaged switch as a permanent motor *communication* fault made
    // ready_to_arm unreachable. Never permit nonzero with the switch set.
    s.failsafe = failsafe ||
        (armed && SRV_Channels::get_emergency_stop()) ||
        _enable != 0 || _probe != 0 || _handshake != 0;
    s.reverse_configured = reverse_configured;
    s.reverse_selected = reverse_selected;
    s.reverse_permitted = reverse_permitted;
    AP_SpektrumSmartESC_Motor::Demand d;
    const uint32_t now = AP_HAL::millis();
    if (!armed) {
        // ArduPlane suppresses or forces SERVO3 while disarmed; it is NOT a
        // raw throttle source or an ESC-idle calibration. Request only zero
        // so the physical ESC can first reply with its stopped telemetry.
        d = {};
        d.rc_ok = rc_ok;
        d.control_ok = !s.failsafe;
        d.failsafe = s.failsafe;
        d.acquired_ms = now;
    } else if (!AP_SpektrumSmartESC_Plane::convert(s,now,d)) {
        d = {};
        d.failsafe = true;
        d.acquired_ms = now;
    }
    d.ground_authorized = _motor_evidence.ground; d.ground_ms = _motor_evidence.ground_ms;
    d.stopped = _motor_evidence.stopped; d.stopped_ms = _motor_evidence.stopped_ms;
    _native_motor->update(d,AP_HAL::micros(),AP_HAL::millis());
#endif
}

void AP_SpektrumSmartESC_Diagnostics::plane_shadow(bool armed, bool rc_ok, bool failsafe,
                                                  bool reverse_available, bool reverse_permitted)
{
    if (_shadow != 1) {
        _plane_intent = {};
        return;
    }
    const uint32_t now = AP_HAL::millis();
    const auto *channel = SRV_Channels::srv_channel(2); // Explicit user SERVO3, never RC3 or SERVO1.
    AP_SpektrumSmartESC_Control::Demand d;
    d.armed = armed;
    d.rc_ok = rc_ok;
    d.failsafe = failsafe;
    d.reverse_permitted = reverse_permitted;
    const bool mapped = channel != nullptr && channel->get_function() == SRV_Channel::k_throttle;
    const uint16_t pwm = mapped ? channel->get_output_pwm() : 0;
    const bool valid = mapped && AP_SpektrumSmartESC_Shadow::plane_demand(pwm,
        channel->get_output_min(), channel->get_trim(), channel->get_output_max(),
        channel->get_reversed(), reverse_available, d);
    AP_SpektrumSmartESC_Plane::Snapshot snapshot;
    snapshot.servo_index = 2;
    if (mapped) {
        snapshot.function = uint16_t(channel->get_function());
        snapshot.pwm = pwm;
        snapshot.minimum = channel->get_output_min();
        snapshot.trim = channel->get_trim();
        snapshot.maximum = channel->get_output_max();
        snapshot.reversed = channel->get_reversed();
    }
    snapshot.bipolar = reverse_available;
    snapshot.armed = armed;
    snapshot.rc_ok = rc_ok;
    snapshot.control_ok = !failsafe;
    snapshot.failsafe = failsafe || SRV_Channels::get_emergency_stop();
    // No validated reverse-source configuration exists yet. In particular,
    // negative PWM-derived demand and raw RC7 never grant direction authority.
    const bool intent_valid = AP_SpektrumSmartESC_Plane::convert(snapshot, now, _plane_intent);
    if (now - _last_shadow_ms < 1000) {
        return;
    }
    _last_shadow_ms = now;
    const uint8_t flags = uint8_t(valid) | (uint8_t(armed) << 1) | (uint8_t(rc_ok) << 2) |
        (uint8_t(failsafe) << 3) | (uint8_t(reverse_permitted) << 4) |
        (uint8_t(SRV_Channels::get_emergency_stop()) << 5);
    // Direct Mission Planner readout for the existing RP4TD -> Plane
    // -> final SERVO3 path. Never treats RC3 raw input as motor authority,
    // never writes to UART4 or a servo output. The bench remains UNARMABLE.
    // Raw RC3 is shown separately from the processed SERVO3 output. It is
    // evidence of RP4TD stick movement, never an actuator command.
    const uint16_t rc3 = RC_Channels::get_radio_in(2);
    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                  "SPKRC IN3=%u S3=%u MAP%u ARM%u RC%u FS%u",
                  unsigned(rc3), unsigned(pwm), unsigned(mapped),
                  unsigned(armed), unsigned(rc_ok), unsigned(failsafe));
    GCS_SEND_TEXT(MAV_SEVERITY_INFO,
                  "SPKRC INT V%u MAG%u REV%u STOP%u",
                  unsigned(intent_valid),
                  unsigned(_plane_intent.magnitude * 100.0f),
                  unsigned(_plane_intent.reverse),
                  unsigned(SRV_Channels::get_emergency_stop()));
#if HAL_LOGGING_ENABLED
    // Demand preview only. No raw channel values without verified ESC calibration.
    AP::logger().WriteStreaming("SPKS", "TimeUS,PWM,Pct,Rev,Flags", "QHfBB",
                               AP_HAL::micros64(), pwm, d.throttle_percent, uint8_t(d.reverse_request), flags);
    AP::logger().WriteStreaming("SPKI", "TimeUS,Valid,Mag,Rev,Phys", "QBfBB", AP_HAL::micros64(),
        uint8_t(intent_valid), _plane_intent.magnitude, uint8_t(_plane_intent.reverse),
        uint8_t(AP_SpektrumSmartESC_Motor::physical_control_available()));
#else
    (void)flags;
    (void)intent_valid;
#endif
}
#endif
