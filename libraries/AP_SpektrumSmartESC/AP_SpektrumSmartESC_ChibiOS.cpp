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
        _active || _pending || (!source_qualified && !bounded_bench)) {
        return false;
    }
    // The special IOMCU may not have published its inhibit readback
    // immediately after BoardConfig. In EarlyNative ONLY, defer the lease
    // and retry while fully DISARMED. Never touch UART4 before IO proof.
    const bool io_prepared = _owner.prepare(nonce, qualification.io_firmware_crc);
    if (!io_prepared && !AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT) {
        return false;
    }
    _qualification = qualification;
    _target = target;
    _uid = uid;
    _pending_ms = now_ms;
    _pending = io_prepared;
    _auto_retry_disarmed = !io_prepared;
    _retry_at_ms = io_prepared ? 0U : now_ms + 100U;
    _retry_requires_framing_quiet = false;
    _startup_framing_retries = 0;
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
        // After a strictly FE-only startup fault, an automatic disarmed
        // reconnect is allowed only if *all* UART errors were framing-only
        // and at least 500ms have elapsed since the last such error.
        // Do not clear the counters or bypass normal UART owner acquisition.
        const bool quiet = !_retry_requires_framing_quiet ||
            _uart.framing_only_fault(true);
        _retry_requires_framing_quiet = false;
        if (!quiet) {
            return; // Other error or continuing FE: latch, no retry.
        }
        const uint32_t nonce = (_uid ^ now_ms) | 1U;
        if (_owner.prepare(nonce, _qualification.io_firmware_crc)) {
            _pending_ms = now_ms;
            _pending = true;
        } else {
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
            // IOMCU proof can arrive late after a cold boot. Keep trying
            // only while disarmed: no UART lease, handshake or motor TX is
            // granted without an actual fresh inhibit receipt.
            _retry_at_ms = now_ms + 1000U;
            _auto_retry_disarmed = true;
#endif
        }
    }
    if (_pending) {
        if (hal.util->get_soft_armed() || uint32_t(now_ms - _pending_ms) >= 200) {
            _pending = false;
            _owner.release();
#if AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
            // Do not permanently strand the battery-on boot when the IO
            // companion hasn't replied within the 200-ms proof window.
            // No motor output can start without independent IO proof.
            if (!_ever_armed && !hal.util->get_soft_armed()) {
                _retry_at_ms = now_ms + 1000U;
                _auto_retry_disarmed = true;
                _retry_requires_framing_quiet = false;
            }
#endif
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

AP_SpektrumSmartESC_ChibiOS::StartupDiagnostics
AP_SpektrumSmartESC_ChibiOS::startup_diagnostics() const
{
    WITH_SEMAPHORE(_lock);
    StartupDiagnostics result {};
    result.fe_attempts = _startup_framing_retries;
    result.retry_pending = _auto_retry_disarmed;
    result.waiting_for_fe_quiet = _retry_requires_framing_quiet;
    result.ever_armed = _ever_armed;
    return result;
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

bool AP_SpektrumSmartESC_ChibiOS::bench_arm_permitted() const
{
#if AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT
    // The EarlyNative commissioning image validates automatic 0x21/0x80
    // link behavior only. No ARM-before-link exception in this board,
    // independent of RC, stick, params and telemetry reports.
    return false;
#elif AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
    WITH_SEMAPHORE(_lock);
    const uint32_t now_ms = AP_HAL::millis();
    const auto state = _motor.status().state;
    return _active && !_pending && !_restore_pending && !_ever_armed &&
        (state == AP_SpektrumSmartESC_Motor::State::DISCOVERY ||
         state == AP_SpektrumSmartESC_Motor::State::IDLE) &&
        _motor.status().fault == AP_SpektrumSmartESC_Motor::Fault::NONE &&
        !_demand.armed && _demand.rc_ok && _demand.control_ok &&
        !_demand.failsafe && _demand.magnitude == 0 && !_demand.reverse &&
        uint32_t(now_ms - _demand.acquired_ms) < 50U &&
        _demand.ground_authorized &&
        uint32_t(now_ms - _demand.ground_ms) < 100U;
#else
    return false;
#endif
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
                    const auto s = _motor.status();
                    const bool disarmed = !_ever_armed && !hal.util->get_soft_armed() &&
                        !_demand.armed;
                    // A FE-only startup glitch (actual 2026-11-13 logs:
                    // F7, FE3/7, TX0, CD0, RX B0/1) may be transient during
                    // ESC boot. Retry at most twice and only after quiet.
                    // A UART fault after any completed frame, nonzero
                    // demand, bad RX classification or ARM still latches.
                    const bool initial_fe =
                        why == AP_SpektrumSmartESC_Motor::Fault::UART &&
                        _demand.magnitude == 0 && !_demand.reverse &&
                        AP_SpektrumSmartESC_UartRecovery::allow_native_fe_startup_retry(
                            _ever_armed, hal.util->get_soft_armed() || _demand.armed,
                            s.tx_frames, s.controls, _startup_framing_retries,
                            _uart.framing_only_fault(false));
                    if (initial_fe) {
                        _startup_framing_retries++;
                        _retry_requires_framing_quiet = true;
                        _retry_at_ms = AP_HAL::millis() + 1000U;
                        _auto_retry_disarmed = true;
                    } else if (disarmed && why == AP_SpektrumSmartESC_Motor::Fault::ESC_LOSS) {
                        _retry_requires_framing_quiet = false;
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
