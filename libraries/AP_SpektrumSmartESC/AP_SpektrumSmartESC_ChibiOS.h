/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_config.h"
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
#include "AP_SpektrumSmartESC_IOOwner.h"
#include "AP_SpektrumSmartESC_Receive.h"
#include <AP_HAL_ChibiOS/DiagnosticRX.h>
#include <AP_HAL_ChibiOS/SpektrumUART4.h>
#include <AP_HAL/Semaphores.h>

// Hardware binding of the existing coordinator. No alternate parser/master or
// SERIALn instance. Ordinary and laboratory builds both deny start().
class AP_SpektrumSmartESC_ChibiOS {
public:
    AP_SpektrumSmartESC_ChibiOS(DiagnosticRX &uart, AP_SpektrumSmartESC_Receive &receiver,
        const AP_SpektrumSmartESC_Commands::Profile &profile) :
        _uart(uart), _receiver(receiver), _port(uart), _motor(_port, _owner, profile) {}
    bool start(const AP_SpektrumSmartESC_Motor::Qualification &qualification, uint8_t target,
               uint32_t uid, uint32_t nonce, uint32_t now_ms);
    void update(const AP_SpektrumSmartESC_Motor::Demand &demand, uint32_t now_us, uint32_t now_ms);
    void stop();
    AP_SpektrumSmartESC_Motor::Status status() const;
    AP_SpektrumSmartESC_Reader::Status handshake_status() const;
    // Read-only, mutex-protected startup retry evidence for GCS. Retries
    // cannot cause ARM or a 0xCD command without real peer/telemetry proof.
    struct StartupDiagnostics {
        uint8_t fe_attempts = 0;
        bool retry_pending = false, waiting_for_fe_quiet = false, ever_armed = false;
    };
    StartupDiagnostics startup_diagnostics() const;
    bool ready_to_arm() const;
    // Explicit non-flight bench ARM monitor: a live, zero-demand discovery
    // lease; never grants native motor output or bypasses normal checks.
    bool bench_arm_permitted() const;
    // Read-only, fresh ESC-reported zero throttle and zero electrical RPM.
    // The sensor payload has no explicit SRXL2 source ID; the production
    // profile additionally requires a qualified isolated one-ESC bus.
    bool stopped_observed() const;
private:
    void worker();
    void stop_locked();
    mutable HAL_Semaphore _lock;
    AP_SpektrumSmartESC_Motor::Demand _demand;
    DiagnosticRX &_uart;
    AP_SpektrumSmartESC_Receive &_receiver;
    AP_SpektrumSmartESC_IOOwner _owner;
    SpektrumUART4 _port;
    AP_SpektrumSmartESC_Motor _motor;
    AP_SpektrumSmartESC_Motor::Qualification _qualification;
    uint32_t _uid = 0, _pending_ms = 0, _retry_at_ms = 0;
    uint32_t _telemetry_sequence = 0;
    uint8_t _target = 0;
    bool _pending = false, _active = false;
    bool _thread_started = false, _restore_pending = false;
    bool _auto_retry_disarmed = false, _ever_armed = false;
    // At most two disarmed, no-control, pure-FE startup retries.
    // Not a generic UART recovery; never used during or after ARM.
    bool _retry_requires_framing_quiet = false;
    uint8_t _startup_framing_retries = 0;
};
#endif
