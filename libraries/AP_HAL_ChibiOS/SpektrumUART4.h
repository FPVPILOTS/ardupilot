/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef SPKTSMART_UART4_REGISTER_TEST
#include "tests/SpektrumUART4_test_hal.h"
#else
#include "DiagnosticRX.h"
#endif

#if HAL_SPEKTRUM_SMART_ESC_UART4_RX
#ifndef SPKTSMART_UART4_REGISTER_TEST
#include <hal.h>
#endif
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Bus.h>
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Lab.h>

// Exclusive UART4 transport. Normal firmware requires electrical qualification.
// Separate waveform/handshake LAB images have strict whitelists and no motor TX.
class SpektrumUART4 : public AP_SpektrumSmartESC_Bus::Port {
public:
    explicit SpektrumUART4(DiagnosticRX &receiver) : _rx(receiver) {}
    ~SpektrumUART4() override;
    bool acquire() override;
    void relinquish() override;
    bool simulated() const override { return false; }
    bool idle_high() const override;
    bool collision() const override;
    bool begin_tx(const uint8_t *data, uint8_t length) override;
    bool transmission_complete() const override;
    bool completion_time_us(uint32_t &time_us) const override
    {
        if (!_done) { return false; }
        time_us = _done_ms * 1000U;
        return true;
    }
    bool release_rx() override;
    static void service_active_locked(); // Called by the dedicated 1-kHz RX timer under system lock.
    static void irq_handler(); // Called only by the gated UART4 TC vector.
private:
    friend class SpektrumUART4RegisterTest;
    friend class DiagnosticRX;
    bool owned() const;
    void service() const; // Caller holds the ChibiOS system lock.
    bool release_rx_locked();
    static void tx_timeout(virtual_timer_t *timer, void *context);
    static SpektrumUART4 *_active;
    DiagnosticRX &_rx;
    mutable const uint8_t *_data = nullptr;
    mutable uint8_t _length = 0;
    mutable uint8_t _sent = 0;
    mutable bool _fault = false;
    mutable bool _done = false;
    uint32_t _error_baseline = 0;
    uint32_t _done_ms = 0;
    virtual_timer_t _timeout;
};
#endif
