/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "AP_SpektrumSmartESC_ChibiOS.h"
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS && defined(HAL_SPEKTRUM_SMART_ESC_UART4_RX)
extern const AP_HAL::HAL &hal;

bool AP_SpektrumSmartESC_ChibiOS::start(const AP_SpektrumSmartESC_Motor::Qualification &qualification,
                                      uint8_t target, uint32_t uid, uint32_t nonce, uint32_t now_ms)
{
    WITH_SEMAPHORE(_lock);
    const bool source_qualified = qualification.electrical && qualification.protocol &&
        qualification.endpoints && qualification.esc_loss_behavior;
    const bool bounded_bench = AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL &&
        qualification.io_firmware_crc == 0x6DBBAD71U &&
        !qualification.reverse_settings;
    if (!AP_SpektrumSmartESC_Motor::physical_control_available() || hal.util->get_soft_armed() ||
        _active || _pending || (!source_qualified && !bounded_bench) ||
        !_owner.prepare(nonce, qualification.io_firmware_crc)) {
        return false;
    }
    _qualification = qualification;
    _target = target;
    _uid = uid;
    _pending_ms = now_ms;
    _pending = true;
    _auto_retry_disarmed = false;
    _ever_armed = false;
    return true;
}

void AP_SpektrumSmartESC_ChibiOS::update(const AP_SpektrumSmartESC_Motor::Demand &demand,
                                       uint32_t now_us, uint32_t now_ms)
{
    WITH_SEMAPHORE(_lock);
    _demand = demand;
    if (demand.armed || hal.util->get_soft_armed()) {
        // No automatic armed relink after a motor, RC or ESC fault.
        _ever_armed = true;
        _auto_retry_disarmed = false;
    }
    if (_restore_pending && !hal.util->get_soft_armed()) {
        _uart.begin(115200);
        _receiver.resume();
        _restore_pending = false;
    }
    // Power-on ordering: if FC booted first and ESC did not reply within
    // the finite handshake window, permit a new *disarmed* attempt after
    // cooldown. No retry after any armed session, UART collision or wrong
    // peer/protocol fault. Arming stays blocked until real zero telemetry.
    if (_auto_retry_disarmed && !_active && !_pending && !_restore_pending &&
        !_ever_armed && !hal.util->get_soft_armed() &&
        int32_t(now_ms - _retry_at_ms) >= 0) {
        _auto_retry_disarmed = false;
        const uint32_t nonce = (_uid ^ now_ms) | 1U;
        if (_owner.prepare(nonce, _qualification.io_firmware_crc)) {
            _pending_ms = now_ms;
            _pending = true;
        }
    }
    if (_pending) {
        if (hal.util->get_soft_armed() || uint32_t(now_ms - _pending_ms) >= 200) {
            _pending = false;
            _owner.release();
            return;
        }
        if (!_owner.acquire(1)) {
            return; // No UART reconfiguration before a real IO readback.
        }
        _receiver.suspend();
        _uart.end();
        _pending = false;
        _active = _motor.start(_qualification, true, _target, _uid, now_us, now_ms);
        if (!_active) {
            stop_locked();
            return;
        }
        if (!_thread_started) {
            _thread_started = hal.scheduler->thread_create(
                FUNCTOR_BIND_MEMBER(&AP_SpektrumSmartESC_ChibiOS::worker, void),
                "SpktESC", 2048, AP_HAL::Scheduler::PRIORITY_RCOUT, 0);
            if (!_thread_started) { stop_locked(); }
        }
    }
}

void AP_SpektrumSmartESC_ChibiOS::stop()
{
    WITH_SEMAPHORE(_lock);
    _auto_retry_disarmed = false;
    _ever_armed = true; // Explicit operator stop cannot self-restart.
    stop_locked();
}

void AP_SpektrumSmartESC_ChibiOS::stop_locked()
{
    _motor.disable();
    _receiver.suspend(); // Invalidate only this producer's existing frontend measurements.
    _owner.release(); // IO pin reservation is immutable; MAIN3 is not re-enabled.
    _pending = _active = false;
    _restore_pending = true; // Peripheral initialization remains on the main thread.
}

AP_SpektrumSmartESC_Motor::Status AP_SpektrumSmartESC_ChibiOS::status() const
{
    WITH_SEMAPHORE(_lock);
    return _motor.status();
}

AP_SpektrumSmartESC_Reader::Status AP_SpektrumSmartESC_ChibiOS::handshake_status() const
{
    WITH_SEMAPHORE(_lock);
    return _motor.reader_status();
}

bool AP_SpektrumSmartESC_ChibiOS::ready_to_arm() const
{
    WITH_SEMAPHORE(_lock);
    const uint32_t now_ms=AP_HAL::millis();
    return _active && !_pending && _demand.rc_ok && _demand.control_ok && !_demand.failsafe &&
        _demand.magnitude >= 0 && _demand.magnitude <= 0 && !_demand.reverse &&
        uint32_t(now_ms-_demand.acquired_ms)<50 && _demand.ground_authorized && _demand.stopped &&
        uint32_t(now_ms-_demand.ground_ms)<100 && uint32_t(now_ms-_demand.stopped_ms)<50 &&
        _motor.ready_to_arm(now_ms,AP_HAL::micros());
}

bool AP_SpektrumSmartESC_ChibiOS::stopped_observed() const
{
    WITH_SEMAPHORE(_lock);
    if (!_active || _pending) { return false; }
    AP_SpektrumSmartESC::Telemetry t {};
    if (!_motor.telemetry(t, AP_HAL::micros())) { return false; }
    constexpr uint16_t required =
        AP_SpektrumSmartESC::ERPM | AP_SpektrumSmartESC::THROTTLE;
    return (t.valid & required) == required &&
           t.electrical_rpm == 0 && t.throttle_pct <= 0.5f;
}

void AP_SpektrumSmartESC_ChibiOS::worker()
{
    // Plane's default main loop is 50 Hz. Do not derive an 11-ms bus cadence
    // or a 5-ms reply window from that loop; only final demand is sampled there.
    for (;;) {
        {
            WITH_SEMAPHORE(_lock);
            if (_active) {
                // Retire a previously queued UART transmission first: the
                // native driver must restore RX after actual hardware TC
                // before the parser can accept an incoming sensor response.
                // Never run the general control/timeout watchdog *before*
                // consuming already-acquired RX bytes; an otherwise fresh
                // 0x80 reply may be waiting in the UART ring when this
                // worker wakes up after a scheduler delay.
                if (_motor.transmitting()) {
                    _motor.update(_demand, AP_HAL::micros(), AP_HAL::millis());
                }
                uint8_t byte;
                uint32_t timestamp_ms;
                for (uint16_t i=0; i<512 && _uart.read_timed(byte,timestamp_ms); i++) {
                    _motor.feed(byte,timestamp_ms*1000U);
                }
                // Now test freshness against the newest complete CRC-checked
                // input and schedule at most the normal next control frame.
                // All arming, IO-owner, RC and UART interlocks remain in the
                // motor controller itself.
                _motor.update(_demand, AP_HAL::micros(), AP_HAL::millis());
#if HAL_WITH_ESC_TELEM
                AP_SpektrumSmartESC::Telemetry current;
                if (!_motor.telemetry(current,AP_HAL::micros())) { _receiver.invalidate_native(); }
                if (_motor.status().telemetry != _telemetry_sequence) {
                    _telemetry_sequence = _motor.status().telemetry;
                    AP_SpektrumSmartESC::Telemetry sample;
                    const uint32_t now_us=AP_HAL::micros(), now_ms=AP_HAL::millis();
                    if (!_motor.status().simulated && _motor.telemetry(sample,now_us)) {
                        _receiver.publish_native(sample,now_ms-_motor.telemetry_age_us(now_us)/1000U,now_ms);
                    }
                }
#endif
                if (_motor.status().state == AP_SpektrumSmartESC_Motor::State::FAULT) {
                    const auto why = _motor.status().fault;
                    // Only NO ESC / stale solicited telemetry while never
                    // armed is eligible for deferred handshake retry.
                    // UART/ownership/protocol/RC/arming faults latch shut.
                    if (why == AP_SpektrumSmartESC_Motor::Fault::ESC_LOSS &&
                        !_ever_armed && !hal.util->get_soft_armed()) {
                        _retry_at_ms = AP_HAL::millis() + 4000U;
                        _auto_retry_disarmed = true;
                    }
                    stop_locked();
                }
            }
        }
        hal.scheduler->delay(1);
    }
}
#endif
