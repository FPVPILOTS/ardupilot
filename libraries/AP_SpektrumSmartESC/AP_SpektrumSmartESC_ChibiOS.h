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
    bool ready_to_arm() const;
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
    uint32_t _uid = 0, _pending_ms = 0;
    uint32_t _telemetry_sequence = 0;
    uint8_t _target = 0;
    bool _pending = false, _active = false;
    bool _thread_started = false, _restore_pending = false;
};
#endif
