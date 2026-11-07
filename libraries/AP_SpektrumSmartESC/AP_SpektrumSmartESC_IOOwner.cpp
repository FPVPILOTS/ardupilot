/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_IOOwner.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_IOMCU/AP_IOMCU.h>
#ifndef HAL_SPEKTRUM_SMART_ESC_UART4_RX
#define HAL_SPEKTRUM_SMART_ESC_UART4_RX 0
#endif

extern const AP_HAL::HAL &hal;

bool AP_SpektrumSmartESC_IOOwner::prepare(uint32_t nonce, uint32_t expected_crc)
{
    _epoch = 0;
    _nonce = 0;
#if HAL_WITH_IO_MCU && HAL_SPEKTRUM_SMART_ESC_UART4_RX
    if (!hal.util->get_soft_armed() && AP::iomcu() != nullptr &&
        AP::iomcu()->request_smart_esc_inhibit(nonce, expected_crc)) {
        _nonce = nonce;
        return true;
    }
#endif
    return false;
}

void AP_SpektrumSmartESC_IOOwner::release()
{
#if HAL_WITH_IO_MCU && HAL_SPEKTRUM_SMART_ESC_UART4_RX
    if (AP::iomcu() != nullptr) { AP::iomcu()->cancel_smart_esc_inhibit(_nonce); }
#endif
    _epoch = _nonce = 0;
}

bool AP_SpektrumSmartESC_IOOwner::acquire(uint32_t epoch)
{
    _epoch = 0;
    if (hal.util->get_soft_armed() || epoch == 0) {
        return false;
    }
    _epoch = epoch;
    if (receipt().epoch != epoch) {
        _epoch = 0;
        return false;
    }
    return true;
}

AP_SpektrumSmartESC_Motor::Receipt AP_SpektrumSmartESC_IOOwner::receipt() const
{
    AP_SpektrumSmartESC_Motor::Receipt result;
#if HAL_WITH_IO_MCU && HAL_SPEKTRUM_SMART_ESC_UART4_RX
    AP_IOMCU_SmartESCInhibit proof;
    uint32_t stamp;
    if (_epoch != 0 && AP::iomcu() != nullptr && AP::iomcu()->smart_esc_inhibit(proof, stamp) &&
        proof.matches(_nonce)) {
        result.epoch = _epoch;
        result.io_boot = _nonce; // Boot-scoped echoed lease, NOT a hardware-unique boot ID.
        result.acquired_ms = stamp;
        result.main3_inhibited = result.failsafe_inhibited = result.aux7_reserved = true;
    }
#endif
    return result;
}
