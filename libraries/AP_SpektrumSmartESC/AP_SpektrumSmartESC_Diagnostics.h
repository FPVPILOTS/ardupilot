/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_config.h"
#if AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
#include "AP_SpektrumSmartESC_Receive.h"
#include "AP_SpektrumSmartESC_Bench.h"
#include "AP_SpektrumSmartESC_Probe.h"
#include "AP_SpektrumSmartESC_Reader.h"
#include "AP_SpektrumSmartESC_Plane.h"
#include "AP_SpektrumSmartESC_Lab.h"
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
#include "AP_SpektrumSmartESC_AutoSession.h"
#endif
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#include "AP_SpektrumSmartESC_LabHandshake.h"
#endif
#include <AP_Param/AP_Param.h>
class AP_SpektrumSmartESC_ChibiOS;

class AP_SpektrumSmartESC_Diagnostics {
public:
    AP_SpektrumSmartESC_Diagnostics() { AP_Param::setup_object_defaults(this, var_info); }
    static const AP_Param::GroupInfo var_info[];
    const AP_SpektrumSmartESC_Reader::Status &reader_status() const { return _reader.status(); }
    const AP_SpektrumSmartESC_Motor::Demand &plane_intent() const { return _plane_intent; }
    bool waveform_requested() const { return _waveform.get() != 0; }
    void init();
    void update();
    void plane_shadow(bool armed, bool rc_ok, bool failsafe, bool reverse_available, bool reverse_permitted);
    struct MotorEvidence {
        bool ground = false, stopped = false;
        uint32_t ground_ms = 0, stopped_ms = 0;
    };
    bool prepare_motor(const AP_SpektrumSmartESC_Commands::Profile &profile,
                       const AP_SpektrumSmartESC_Motor::Qualification &qualification,
                       uint8_t target, uint32_t uid, uint32_t nonce);
    bool motor_evidence(const MotorEvidence &evidence);
    void plane_control(bool armed, bool rc_ok, bool failsafe, bool reverse_available,
                       bool reverse_permitted, bool reverse_configured, bool reverse_selected);
    bool arming_blocked() const { return bench_blocked() || (_motor_selected && !motor_arming_ready()); }
private:
    bool bench_blocked() const { return AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB || _bench.arming_blocked(_enable.get()) || _probe.get() != 0 || _handshake.get() != 0; }
    bool motor_arming_ready() const;
    AP_SpektrumSmartESC_ChibiOS *_native_motor = nullptr;
    MotorEvidence _motor_evidence;
    bool update_waveform(uint32_t now_ms);
    AP_Int8 _waveform;
    bool _wave_active = false, _wave_requested = false;
    bool _motor_selected = false; // Failed explicit selection cannot fall back to legacy arming.
    uint32_t _wave_start_ms = 0, _wave_last_ms = 0;
    bool update_reader(uint32_t now_ms);
    void log_reader(uint32_t now_ms);
    AP_Int8 _handshake, _hs_target;
    AP_SpektrumSmartESC_Reader _reader;
    uint8_t _hs_packet[14] {};
    bool _hs_inflight = false, _hs_leased = false;
    uint32_t _last_hs_request_ms = 0, _last_reader_log_ms = 0;
    uint32_t _hs_fault_base = 0;
    // Snapshot before handshake lease creation: only new UART errors abort.
    uint32_t _hs_uart_error_baseline = 0;
    uint8_t _hs_logged_bytes = 0;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    AP_Int8 _hs_ack;
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    AP_Int8 _auto;
    AP_SpektrumSmartESC_AutoSession _auto_session;
    AP_SpektrumSmartESC _auto_rx {0x30, 0x40, 50, false};
    uint8_t _auto_packet[14] {};
    uint32_t _auto_uart_error_baseline = 0;
    uint32_t _auto_log_ms = 0;
    bool _auto_started = false;
    bool _auto_leased = false, _auto_inflight = false;
    void update_auto_reader(uint32_t now_ms);
#endif
    AP_SpektrumSmartESC_LabHandshake::Authorization _hs_authorization;
    bool _hs_lab_session = false, _hs_window_reported = false;
    uint32_t _hs_lab_status_ms = 0;
    void log_handshake_lab(uint32_t now_ms);
#endif
    bool _hs_power_on = false;
    bool update_probe(uint32_t now_ms); // true means GPIO/blocked probe owns this update.
    bool log_probe(uint32_t now_ms);
    AP_Int8 _probe;
    AP_SpektrumSmartESC_Probe _probe_policy;
    uint8_t _probe_applied = 0;
    bool _probe_report_pending = false;
    uint32_t _last_probe_log_ms = 0, _last_probe_message_ms = 0;
    AP_Int8 _enable;
    AP_Int8 _telem;
    AP_Int8 _shadow;
    AP_SpektrumSmartESC_Motor::Demand _plane_intent;
    uint32_t _last_shadow_ms = 0;
    AP_Int32 _experiment;
    AP_SpektrumSmartESC_Bench _bench;
    AP_SpektrumSmartESC_Receive *_receiver = nullptr;
    uint32_t _last_log_ms = 0;
    uint32_t _last_message_ms = 0;
};
#endif
