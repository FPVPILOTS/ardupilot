/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef SPKTSMART_RX_REGISTER_TEST
#include "tests/DiagnosticRX_test_hal.h"
#else
#include <AP_HAL/AP_HAL.h>
#endif

#ifndef HAL_SPEKTRUM_SMART_ESC_UART4_RX
#define HAL_SPEKTRUM_SMART_ESC_UART4_RX 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED
#define AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED 0
#endif

#if HAL_SPEKTRUM_SMART_ESC_UART4_RX
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Lab.h>
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_UartRecovery.h>
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_LabHandshake.h>
#endif
class SpektrumUART4;

// Dedicated H743 UART4 receiver on PA0. No SerialDriver startup: ChibiOS
// sdStart() forcibly enables TE, which is inappropriate for this receiver.
class DiagnosticRX : public AP_HAL::UARTDriver {
public:
    bool is_receive_only() const override { return _experimental_owner == nullptr; }
    bool is_initialized() override;
    // Dedicated backend only: stopped-port GPIO input, optional weak RX pull.
    // No inversion, TX options, direction output or arbitrary pin selection.
    bool set_options(uint16_t options) override;
    bool diagnostic_handshake(const uint8_t *packet, uint8_t length, bool finish) override;
    bool diagnostic_handshake_start(uint8_t target, uint32_t uid, bool power_on = false) override;
    bool diagnostic_waveform(bool start, bool finish) override;
    bool tx_pending() override { return false; }
    uint32_t txspace() override { return 0; }
    bool read_timed(uint8_t &byte, uint32_t &timestamp_ms) override;
    uint32_t receive_errors() const override;
    bool receive_diagnostics(ReceiveDiagnostics &status) const override;
protected:
    void _begin(uint32_t baud, uint16_t rx, uint16_t tx) override;
    void _end() override;
    void _flush() override {}
    size_t _write(const uint8_t *, size_t) override { return 0; }
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    uint32_t _available() override;
    bool _discard_input() override;
private:
    bool diagnostic_transaction(const uint8_t *packet, uint8_t length, bool finish);
    friend class SpektrumUART4;
#ifdef SPKTSMART_RX_REGISTER_TEST
    friend class DiagnosticRXRegisterTest;
#endif
    // A future gated bidirectional session must lease this same receiver;
    // never initialize a second independent driver for the reserved UART4.
    const void *_experimental_owner = nullptr;
    SpektrumUART4 *_handshake_port = nullptr;
    uint32_t _handshake_faults = 0;
    uint32_t _handshake_queued = 0, _handshake_completed = 0, _handshake_restored = 0;
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    AP_SpektrumSmartESC_LabHandshake::Lease _lab_hs_lease;
#endif
    void poll();
    void register_poll();
    void start_samples(uint8_t mode); // Called with system locked.
    void safe_input(); // Called with system locked; no output/high/low writes.
    uint8_t _probe_mode = 0, _probe_levels = 0;
    bool _probe_last_high = false;
    uint32_t _probe_started_ms = 0, _probe_duration_ms = 0, _probe_transitions = 0;
    struct Byte {
        uint32_t ms;
        uint8_t value;
    };
    static constexpr uint16_t SIZE = 512;
    Byte _buffer[SIZE] {};
    uint16_t _head = 0;
    uint16_t _tail = 0;
    bool _initialized = false;
    bool _registered = false;
    uint32_t _errors = 0;
    AP_SpektrumSmartESC_UartRecovery _uart_recovery;
    uint32_t _framing = 0, _noise = 0, _parity = 0, _overruns = 0, _overflow = 0;
    uint32_t _release_errors = 0;
};
#endif
