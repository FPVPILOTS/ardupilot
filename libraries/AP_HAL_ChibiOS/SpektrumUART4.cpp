/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "SpektrumUART4.h"
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Reader.h>
#include <AP_SpektrumSmartESC/AP_SpektrumSmartESC_Motor.h>

#if HAL_SPEKTRUM_SMART_ESC_UART4_RX
#ifndef SPKTSMART_UART4_REGISTER_TEST
#include <hal.h>
#endif
extern const AP_HAL::HAL &hal;
SpektrumUART4 *SpektrumUART4::_active;

// No IRQ is allocated in the shipped, unverified build. The handler's logic
// below is still compiled and type-checked with the actual H743 headers.
#if (AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) && !defined(SPKTSMART_UART4_REGISTER_TEST)
OSAL_IRQ_HANDLER(STM32_UART4_HANDLER);
OSAL_IRQ_HANDLER(STM32_UART4_HANDLER)
{
    OSAL_IRQ_PROLOGUE();
    SpektrumUART4::irq_handler();
    OSAL_IRQ_EPILOGUE();
}
#endif

SpektrumUART4::~SpektrumUART4()
{
    relinquish();
}

bool SpektrumUART4::owned() const
{
    return _rx._experimental_owner == this;
}

bool SpektrumUART4::acquire()
{
    // This constant check also guards initialization, not just TDR writes.
    if (!(AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED || AP_SPEKTRUM_SMART_ESC_LAB_ONLY || AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB) ||
        !hal.scheduler->in_main_thread()) {
        return false;
    }
    chSysLock();
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    if (!_rx._lab_hs_lease.active(AP_HAL::millis()) &&
        !_rx._poll_lab_lease.active(AP_HAL::millis())) {
        chSysUnlock();
        return false;
    }
#endif
    if (_active != nullptr || _rx._initialized || _rx._experimental_owner != nullptr) {
        chSysUnlock();
        return false;
    }
    _rx._experimental_owner = this;
    chVTObjectInit(&_timeout);
    _active = this;
    chSysUnlock();
    _rx._begin(115200, 0, 0);
    if (!_rx.is_initialized()) {
        relinquish();
        return false;
    }
    _fault = false;
    _error_baseline = _rx.receive_errors();
    nvicEnableVector(STM32_UART4_NUMBER, STM32_UART_UART4_IRQ_PRIORITY);
    if (!release_rx()) {
        relinquish();
        return false;
    }
    return true;
}

void SpektrumUART4::relinquish()
{
    chSysLock();
    if (!owned()) {
        chSysUnlock();
        return;
    }
    // Detach PA0 first, just as a timer transaction disables its output before
    // configuring input capture. Never restore TIM5/PWM on this reserved pin.
    palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_INPUT);
    nvicDisableVector(STM32_UART4_NUMBER);
    chVTResetI(&_timeout);
    if (_rx._initialized) {
        UART4->CR1 = 0;
        rccDisableUART4();
    }
    _rx._initialized = false;
    _rx._head = _rx._tail = 0;
    _rx._experimental_owner = nullptr;
    _active = nullptr;
    _data = nullptr;
    _length = _sent = 0;
    chSysUnlock();
}

bool SpektrumUART4::idle_high() const
{
    chSysLock();
    const bool idle = owned() && _rx._initialized && _data == nullptr &&
                      !(UART4->ISR & USART_ISR_BUSY) && palReadLine(PAL_LINE(GPIOA, 0));
    chSysUnlock();
    return idle;
}

void SpektrumUART4::service() const
{
    if (!owned() || !_rx._initialized) {
        _fault = true;
        return;
    }
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    if (!_rx._lab_hs_lease.active(AP_HAL::millis()) &&
        !_rx._poll_lab_lease.active(AP_HAL::millis())) {
        _fault = true;
        return;
    }
#endif
    if (_rx._errors != _error_baseline || !(UART4->CR3 & USART_CR3_HDSEL) ||
        (UART4->CR2 & USART_CR2_SWAP) ||
        (GPIOA->MODER & 3U) != 2U || (GPIOA->AFRL & 15U) != 8U || !(GPIOA->OTYPER & 1U) ||
        (UART4->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))) {
        _fault = true;
        return;
    }
    if (_data == nullptr || !(UART4->ISR & USART_ISR_TEACK)) {
        return;
    }
    // At most one H7 FIFO's worth, no refill deadline or bit-banged timing.
    // The bus layer retains this buffer until TC or explicit abort/release.
    for (uint8_t i = 0; i < 16 && _sent < _length && (UART4->ISR & USART_ISR_TXE_TXFNF); i++) {
        UART4->TDR = _data[_sent++];
    }
    if (_sent == _length) {
        // TC interrupt, not a millisecond vehicle task, performs turnaround.
        UART4->CR1 |= USART_CR1_TCIE;
    }
}

void SpektrumUART4::service_active_locked()
{
    if (_active != nullptr && _active->_rx._initialized) {
        _active->service(); // Fills the single 14-byte FIFO after a delayed TEACK.
        if (_active->_fault) {
            _active->release_rx_locked();
        }
    }
}

void SpektrumUART4::irq_handler()
{
    chSysLockFromISR();
    if (_active != nullptr && _active->_rx._initialized &&
        (UART4->CR1 & USART_CR1_TCIE) && (UART4->ISR & USART_ISR_TC)) {
        SpektrumUART4 &port = *_active;
        const bool sent = port._data != nullptr && port._sent == port._length && !port._fault;
        // Always release/disable TCIE, including unexpected completion/faults.
        // Short-circuiting on `sent` would leave an asserted IRQ and driver.
        const bool released = port.release_rx_locked();
        port._done = sent && released;
        if (port._done) {
            port._rx._handshake_completed++;
        }
        port._done_ms = AP_HAL::millis();
        port._fault = port._fault || !port._done;
    }
    chSysUnlockFromISR();
}

void SpektrumUART4::tx_timeout(virtual_timer_t *, void *context)
{
    auto &port = *static_cast<SpektrumUART4 *>(context);
    chSysLockFromISR();
    if (port.owned()) {
        port._fault = true;
        port._done = false;
        port.release_rx_locked();
    }
    chSysUnlockFromISR();
}

bool SpektrumUART4::collision() const
{
    chSysLock();
    service();
    const bool fault = _fault;
    chSysUnlock();
    // UART errors are detectable; electrical contention during TX is NOT
    // reliably detectable in this mode. The idle guard prevents known overlap.
    return fault;
}

bool SpektrumUART4::begin_tx(const uint8_t *data, uint8_t length)
{
    const bool allowed = AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB ?
        (AP_SpektrumSmartESC_Reader::handshake_packet(data, length) ||
         (AP_SPEKTRUM_SMART_ESC_POLL_LAB &&
          AP_SpektrumSmartESC_PollLab::exact_packet(data, length))) :
        AP_SPEKTRUM_SMART_ESC_LAB_ONLY ? AP_SpektrumSmartESC_Lab::packet(data, length) :
        ((AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED && AP_SpektrumSmartESC_Reader::handshake_packet(data, length)) ||
         (AP_SpektrumSmartESC_Motor::physical_control_available() && AP_SpektrumSmartESC_Motor::control_packet(data, length)));
    if (!allowed) {
        return false;
    }
    chSysLock();
    if (!owned() || !_rx._initialized || _fault || _data != nullptr ||
        !palReadLine(PAL_LINE(GPIOA, 0)) || (UART4->ISR & USART_ISR_BUSY)) {
        chSysUnlock();
        return false;
    }
    // Do not discard an unread peer packet to make transmission possible.
    if (_rx._head != _rx._tail || (UART4->ISR & USART_ISR_RXNE_RXFNE)) {
        chSysUnlock();
        return false;
    }
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
    // Check errors/mux before enabling TE, then consume the native frame budget.
    service();
    const uint32_t direction = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE | USART_CR1_TCIE | USART_CR1_FIFOEN;
    if (_fault || (UART4->CR1 & direction) != (USART_CR1_UE | USART_CR1_RE | USART_CR1_FIFOEN) ||
        !(AP_SPEKTRUM_SMART_ESC_POLL_LAB &&
          AP_SpektrumSmartESC_PollLab::exact_packet(data, length) ?
          _rx._poll_lab_lease.take(data, length, AP_HAL::millis()) :
          _rx._lab_hs_lease.take(data, length, AP_HAL::millis()))) {
        chSysUnlock();
        return false;
    }
#endif
    _data = data;
    _length = length;
    _sent = 0;
    _done = false;
    _rx._handshake_queued++;
    UART4->CR1 &= ~USART_CR1_RE;
    UART4->ICR = USART_ICR_TCCF;
    UART4->CR1 |= USART_CR1_TE;
    // Like bdshot_receive_pulses_DMAR: arm a bounded hardware transaction even
    // if the main thread subsequently stalls. This is not a bus response timer.
    chVTSetI(&_timeout, chTimeUS2I(3000), tx_timeout, this);
    service();
    const bool started = !_fault;
    chSysUnlock();
    return started;
}

bool SpektrumUART4::transmission_complete() const
{
    chSysLock();
    service();
    const bool complete = !_fault && _done;
    chSysUnlock();
    return complete;
}

bool SpektrumUART4::release_rx()
{
    chSysLock();
    const bool receive = release_rx_locked();
    chSysUnlock();
    return receive;
}

bool SpektrumUART4::release_rx_locked()
{
    if (!owned() || !_rx._initialized) {
        return false;
    }
    const uint32_t receive_bits = USART_CR1_UE | USART_CR1_RE;
    const uint32_t direction_bits = receive_bits | USART_CR1_TE | USART_CR1_TCIE;
    const bool configured = (UART4->CR3 & USART_CR3_HDSEL) && !(UART4->CR2 & USART_CR2_SWAP);
    if (_data == nullptr && (UART4->CR1 & direction_bits) == receive_bits && configured &&
        (GPIOA->MODER & 3U) == 2U && (GPIOA->AFRL & 15U) == 8U && (GPIOA->OTYPER & 1U)) {
        // TC ISR already released RX. Do not flush a response that arrived
        // before the vehicle task acknowledges completion (or during acquire).
        chVTResetI(&_timeout);
        return true;
    }
    const bool was_transmitting = _data != nullptr;
    // Input mode immediately disconnects the UART from the pad, including
    // during an abort. Do not assume TE-clear is instantaneous at the pad.
    palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_INPUT);
    chVTResetI(&_timeout);
    UART4->CR1 &= ~(USART_CR1_TE | USART_CR1_RE | USART_CR1_TCIE);
    UART4->RQR = USART_RQR_TXFRQ | USART_RQR_RXFRQ;
    UART4->ICR = 0xFFFFFFFFU;
    UART4->CR1 |= USART_CR1_RE;
    _data = nullptr;
    _length = _sent = 0;
    const bool receive = (UART4->CR1 & direction_bits) == receive_bits && configured;
    if (!receive) {
        _rx._release_errors++;
        _rx._errors++;
    }
    if (receive) {
        palSetLineMode(PAL_LINE(GPIOA, 0), PAL_MODE_ALTERNATE(8) | PAL_STM32_OTYPE_OPENDRAIN);
        if (was_transmitting) {
            _rx._handshake_restored++;
        }
    }
    return receive;
}
#endif
