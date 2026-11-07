/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "DiagnosticRX.h"
#ifndef SPKTSMART_RX_REGISTER_TEST
#include "SpektrumUART4.h"
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Reader.h>
#endif

#if HAL_SPEKTRUM_SMART_ESC_UART4_RX
#ifndef SPKTSMART_RX_REGISTER_TEST
#include <hal.h>
#endif

#if !defined(STM32H7) || STM32_SERIAL_USE_UART4 || STM32_SIO_USE_UART4
#error "DiagnosticRX requires a board-reserved H7 UART4 with no other driver"
#endif
#if defined(STM32_UART_USE_UART4) && STM32_UART_USE_UART4
#error "UART4 is owned by the ChibiOS UART driver"
#endif

extern const AP_HAL::HAL &hal;

void DiagnosticRX::_begin(uint32_t baud, uint16_t, uint16_t)
{
    if (_initialized || baud != 115200 || hal.util->get_soft_armed() || !hal.scheduler->in_main_thread()) {
        return;
    }
    // Keep PA0 disconnected from the peripheral until TE is known to be off.
    chSysLock();
    safe_input();
    rccEnableUART4(true);
    rccResetUART4();
    UART4->CR1 = 0;
    UART4->CR2 = 0;
    UART4->CR3 = USART_CR3_HDSEL;
    UART4->PRESC = 0;
    UART4->BRR = (STM32_UART4CLK + baud / 2U) / baud;
    UART4->ICR = 0xFFFFFFFFU;
    // TE, DMA and all interrupt enables remain clear, including at startup.
    constexpr uint32_t RX_CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_FIFOEN;
    static_assert((RX_CR1 & USART_CR1_TE) == 0, "Receive-only UART must never enable TE");
    UART4->CR1 = RX_CR1;
    if ((UART4->CR1 & (USART_CR1_UE | USART_CR1_RE | USART_CR1_TE)) !=
        (USART_CR1_UE | USART_CR1_RE) || !(UART4->CR3 & USART_CR3_HDSEL)) {
        UART4->CR1 = 0;
        _errors++;
        _release_errors++;
        rccDisableUART4();
        chSysUnlock();
        return;
    }
    palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_ALTERNATE(8) | PAL_STM32_OTYPE_OPENDRAIN);
    _head = _tail = 0;
    _initialized = true;
    start_samples(4);
    chSysUnlock();
    register_poll();
}

void DiagnosticRX::register_poll()
{
    if (!_registered) {
        hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&DiagnosticRX::poll, void));
        _registered = true;
    }
}

bool DiagnosticRX::is_initialized()
{
    chSysLock();
    const bool initialized = _initialized;
    chSysUnlock();
    return initialized;
}

uint32_t DiagnosticRX::receive_errors() const
{
    chSysLock();
    const uint32_t errors = _errors;
    chSysUnlock();
    return errors;
}

bool DiagnosticRX::receive_diagnostics(ReceiveDiagnostics &status) const
{
    chSysLock();
    status = {};
    // Do not read UART registers while its clock is disabled.
    if (_initialized) {
        status.cr1 = UART4->CR1;
        status.cr3 = UART4->CR3;
        status.isr = UART4->ISR;
        status.flags |= 1;
    }
    if (palReadLine(PAL_LINE(GPIOA, 0))) {
        status.flags |= 2;
    }
    if ((GPIOA->MODER & 3U) == 0) {
        status.flags |= 4;
    }
    if ((GPIOA->MODER & 3U) == 2U && (GPIOA->AFRL & 15U) == 8U && (GPIOA->OTYPER & 1U)) {
        status.flags |= 8;
    }
    status.handshake_faults = _handshake_faults;
    status.handshake_queued = _handshake_queued;
    status.handshake_completed = _handshake_completed;
    status.handshake_restored = _handshake_restored;
    status.handshake_flags = AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED ? 1 : 0;
    status.handshake_flags |= AP_SPEKTRUM_SMART_ESC_LAB_ONLY ? 8 : 0;
    status.handshake_flags |= AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB ? 16 : 0;
    // Bit 6: historical UART error state is eligible for a fresh LAB attempt.
    // This does not bypass hardware ISR, FIFO or ownership checks at start.
    if (_uart_recovery.eligible(_errors, AP_HAL::millis())) {
        status.handshake_flags |= 64;
    }
    if (_initialized && _head == _tail && palReadLine(PAL_LINE(GPIOA, 0)) &&
        !(UART4->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE)) &&
        (UART4->CR1 & (USART_CR1_TE | USART_CR1_RE)) == USART_CR1_RE) {
        status.handshake_flags |= 32;
    }
#ifndef SPKTSMART_RX_REGISTER_TEST
    if ((AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) &&
        _handshake_port != nullptr && _handshake_port->owned()) {
        status.handshake_flags |= 2;
        if (_handshake_port->_done) {
            status.handshake_flags |= 4;
            status.handshake_tc_ms = _handshake_port->_done_ms;
        }
    }
#endif
    status.probe_mode = _probe_mode;
    status.probe_levels = _probe_levels;
    status.probe_transitions = _probe_transitions;
    status.probe_duration_ms = _probe_duration_ms;
    status.framing = _framing;
    status.noise = _noise;
    status.parity = _parity;
    status.overruns = _overruns;
    status.overflow = _overflow;
    status.release_errors = _release_errors;
    chSysUnlock();
    return true;
}

void DiagnosticRX::safe_input()
{
    // Disconnect peripheral from PA0 before disabling UART clock.
    palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_INPUT);
    if (_initialized) {
        UART4->CR1 = 0;
        rccDisableUART4();
    }
    _initialized = false;
    _last_options = 0;
    _probe_mode = 0;
    _head = _tail = 0;
}

void DiagnosticRX::_end()
{
    chSysLock();
    safe_input();
    chSysUnlock();
}

void DiagnosticRX::start_samples(uint8_t mode)
{
    _probe_mode = mode;
    _probe_started_ms = AP_HAL::millis();
    _probe_duration_ms = _probe_transitions = 0;
    _probe_last_high = palReadLine(PAL_LINE(GPIOA, 0));
    _probe_levels = _probe_last_high ? 2 : 1;
}

bool DiagnosticRX::set_options(uint16_t options)
{
    if (hal.util->get_soft_armed() || !hal.scheduler->in_main_thread()) {
        return false; // Operator-requested transitions while armed are forbidden.
    }
    chSysLock();
    if (_experimental_owner != nullptr || _initialized) {
        chSysUnlock();
        return false; // Stop RX and release its existing owner first.
    }
    safe_input();
    uint8_t mode = 1;
    uint32_t pull = 0;
    if (options == OPTION_PULLUP_RX) {
        mode = 2; pull = 1;
        palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_INPUT_PULLUP);
    } else if (options == OPTION_PULLDOWN_RX) {
        mode = 3; pull = 2;
        palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_INPUT_PULLDOWN);
    } else if (options != 0) {
        _release_errors++; _errors++;
        chSysUnlock();
        return false;
    }
    const bool valid = (GPIOA->MODER & 3U) == 0 && (GPIOA->PUPDR & 3U) == pull;
    if (!valid) {
        safe_input();
        _release_errors++; _errors++;
    } else {
        _last_options = options;
        start_samples(mode);
    }
    chSysUnlock();
    if (valid) {
        register_poll();
    }
    return valid;
}

bool DiagnosticRX::diagnostic_handshake(const uint8_t *packet, uint8_t length, bool finish)
{
#if (AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) && !AP_SPEKTRUM_SMART_ESC_LAB_ONLY
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    chSysLock();
    if (finish) {
        _lab_hs_lease.close();
    }
    const bool permitted = finish || _lab_hs_lease.active(AP_HAL::millis());
    chSysUnlock();
    if (!permitted) {
        diagnostic_transaction(nullptr, 0, true);
        return false;
    }
#endif
    if (packet != nullptr && !AP_SpektrumSmartESC_Reader::handshake_packet(packet, length)) {
        return false; // No parameter can override this electrical qualification gate.
    }
    return diagnostic_transaction(packet, length, finish);
#else
    (void)packet; (void)length; (void)finish;
    return false;
#endif
}

bool DiagnosticRX::diagnostic_handshake_start(uint8_t target, uint32_t uid, bool power_on)
{
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    if (hal.util->get_soft_armed() || !hal.scheduler->in_main_thread()) {
        return false;
    }
    chSysLock();
    const uint32_t direction = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE | USART_CR1_TCIE;
    // A historical FE-only event can settle; every other error type stays
    // latched out. Total error counters remain unchanged for diagnosis.
    const bool ready = _initialized && _experimental_owner == nullptr &&
        _uart_recovery.eligible(_errors, AP_HAL::millis()) &&
        _head == _tail && (power_on || palReadLine(PAL_LINE(GPIOA, 0))) &&
        (GPIOA->MODER & 3U) == 2U && (GPIOA->AFRL & 15U) == 8U && (GPIOA->OTYPER & 1U) &&
        (UART4->CR1 & direction) == (USART_CR1_UE | USART_CR1_RE) &&
        (UART4->CR3 & USART_CR3_HDSEL) && !(UART4->CR2 & USART_CR2_SWAP) &&
        !(UART4->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE | USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE));
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    // Only AutoLab permits another bounded HANDSHAKE lease, and only after
    // the prior native owner released the port. Regular HSLab stays one-shot.
    const uint32_t now_ms = AP_HAL::millis();
    const bool cooldown = _auto_hs_last_ms == 0 || uint32_t(now_ms - _auto_hs_last_ms) >= 1000;
    if (ready && cooldown && _lab_hs_lease.used() && !_lab_hs_lease.active(now_ms) &&
        (_handshake_port == nullptr || !_handshake_port->owned())) {
        _lab_hs_lease = AP_SpektrumSmartESC_LabHandshake::Lease{};
    }
    const bool granted = ready && cooldown && _lab_hs_lease.start(target, uid, now_ms, power_on);
    if (granted) {
        _auto_hs_last_ms = now_ms;
    }
#else
    const bool granted = ready && _lab_hs_lease.start(target, uid, AP_HAL::millis(), power_on);
#endif
    chSysUnlock();
    return granted;
#else
    (void)target; (void)uid; (void)power_on;
    return false;
#endif
}

bool DiagnosticRX::diagnostic_handshake_startup_observed(const uint8_t *packet, uint8_t length)
{
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
    if (hal.util->get_soft_armed() || !hal.scheduler->in_main_thread()) {
        return false;
    }
    chSysLock();
    const bool allowed = _initialized && _experimental_owner == nullptr &&
        _lab_hs_lease.authorize_startup_announcement(packet, length, AP_HAL::millis());
    chSysUnlock();
    return allowed;
#else
    (void)packet; (void)length;
    return false;
#endif
}

bool DiagnosticRX::diagnostic_waveform(bool start, bool finish)
{
    if (!AP_SPEKTRUM_SMART_ESC_LAB_ONLY) {
        return false;
    }
    static const uint8_t pattern[] = {0x55, 0xAA, 0x55, 0xAA};
    return diagnostic_transaction(start ? pattern : nullptr, start ? sizeof(pattern) : 0, finish);
}

bool DiagnosticRX::diagnostic_transaction(const uint8_t *packet, uint8_t length, bool finish)
{
    if (!(AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) ||
        !hal.scheduler->in_main_thread()) {
        return false;
    }
#ifndef SPKTSMART_RX_REGISTER_TEST
    if (finish || hal.util->get_soft_armed()) {
        if (_handshake_port != nullptr) {
            _handshake_port->relinquish();
        }
        if (!hal.util->get_soft_armed()) {
            _begin(115200, 0, 0); // Restore RE-only after detaching exclusive owner.
        }
        return false;
    }
    if (packet != nullptr) {
        if (_handshake_port == nullptr) {
            _handshake_port = NEW_NOTHROW SpektrumUART4(*this);
        }
        if (_handshake_port == nullptr) {
            return false;
        }
        if (!_handshake_port->owned()) {
            chSysLock();
            const bool quiet = _experimental_owner == nullptr && _head == _tail &&
                (!_initialized || !(UART4->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE))) &&
                palReadLine(PAL_LINE(GPIOA, 0));
            chSysUnlock();
            if (!quiet) {
                return false; // Never discard another device's packet to acquire the bus.
            }
            _end();
            if (!_handshake_port->acquire()) {
                _handshake_faults++;
                _begin(115200, 0, 0);
                return false;
            }
        }
        if (!_handshake_port->begin_tx(packet, length)) {
            _handshake_faults++;
            return false;
        }
    }
    if (_handshake_port == nullptr || !_handshake_port->owned()) {
        return false;
    }
    if (_handshake_port->collision()) {
        _handshake_faults++;
        return false;
    }
    return true;
#else
    (void)packet; (void)length; (void)finish;
    return false;
#endif
}

void DiagnosticRX::poll()
{
    chSysLock();
#ifndef SPKTSMART_RX_REGISTER_TEST
    if (AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) {
        SpektrumUART4::service_active_locked();
    }
#endif
    if (_probe_mode != 0) {
        const uint32_t now = AP_HAL::millis();
        const bool high = palReadLine(PAL_LINE(GPIOA, 0));
        _probe_transitions += high != _probe_last_high;
        _probe_last_high = high;
        _probe_levels |= high ? 2 : 1;
        _probe_duration_ms = now - _probe_started_ms;
        const uint32_t pull = _probe_mode == 2 ? 1 : (_probe_mode == 3 ? 2 : 0);
        const bool bad_gpio = _probe_mode != 4 &&
            ((GPIOA->MODER & 3U) != 0 || (GPIOA->PUPDR & 3U) != pull);
        if (hal.util->get_soft_armed() || bad_gpio) {
            safe_input();
            _errors++; _release_errors++;
            chSysUnlock();
            return;
        }
    }
    if (_initialized) {
        // Like the output/capture ownership checks in RCOutput, stop rather
        // than continuing under an unexpected peripheral configuration.
        // The experimental port may enable TE only in a validated build.
        const bool unexpected_tx = (UART4->CR1 & USART_CR1_TE) &&
                                  (_experimental_owner == nullptr ||
                                   !(AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB));
        const bool wrong_mux = (GPIOA->MODER & 3U) != 2U || (GPIOA->AFRL & 15U) != 8U ||
                               !(GPIOA->OTYPER & 1U);
        if (unexpected_tx || wrong_mux || (UART4->CR2 & USART_CR2_SWAP) || !(UART4->CR3 & USART_CR3_HDSEL)) {
            safe_input();
            _errors++;
            _release_errors++;
            chSysUnlock();
            return;
        }
        const uint32_t status = UART4->ISR;
        if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
            _errors++;
            // Only an isolated framing error is eligible for delayed recovery.
            // Never classify noise, parity, overrun or combined flags as safe.
            if ((status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) == USART_ISR_FE) {
                _uart_recovery.record_framing_only(AP_HAL::millis());
            }
            _overruns += (status & USART_ISR_ORE) != 0;
            _framing += (status & USART_ISR_FE) != 0;
            _noise += (status & USART_ISR_NE) != 0;
            _parity += (status & USART_ISR_PE) != 0;
            UART4->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        }
        const uint32_t now = AP_HAL::millis();
        // H7 FIFO is 16 bytes. Bounded 1-kHz polling, 115200 only. Overruns are
        // diagnostic errors, never silently interpreted as a healthy link.
        for (uint8_t i = 0; i < 16 && (UART4->ISR & USART_ISR_RXNE_RXFNE); i++) {
            const uint8_t value = UART4->RDR;
            const uint16_t next = (_head + 1U) % SIZE;
            if (next == _tail) {
                _errors++;
                _overflow++;
                continue;
            }
            _buffer[_head] = {now, value};
            _head = next;
        }
    }
    chSysUnlock();
}

bool DiagnosticRX::read_timed(uint8_t &byte, uint32_t &timestamp_ms)
{
    chSysLock();
    const bool have_byte = _head != _tail;
    if (have_byte) {
        byte = _buffer[_tail].value;
        timestamp_ms = _buffer[_tail].ms;
        _tail = (_tail + 1U) % SIZE;
    }
    chSysUnlock();
    return have_byte;
}

ssize_t DiagnosticRX::_read(uint8_t *buffer, uint16_t count)
{
    if (count > SIZE) {
        count = SIZE;
    }
    uint32_t timestamp;
    uint16_t n = 0;
    while (n < count && read_timed(buffer[n], timestamp)) {
        n++;
    }
    return n;
}

uint32_t DiagnosticRX::_available()
{
    chSysLock();
    const uint32_t count = (_head + SIZE - _tail) % SIZE;
    chSysUnlock();
    return count;
}

bool DiagnosticRX::_discard_input()
{
    chSysLock();
    _tail = _head;
    chSysUnlock();
    return true;
}
#endif
