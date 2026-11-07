# Phase 15C — historical UART framing error recovery (LAB only)

## Source of this change
Mission Planner's 2026-11-07 capture showed `SPKT UART FE1 ERR1`, followed by
`SPK15 BLOCKED_UART_ERROR` with `QUEUED0 TC0 RXREST0`. The passive parser
later observed `ID40 T21 B84 F6`. This is evidence of an incoming SRXL2
handshake stream, **not** evidence that the FC transmitted or completed a
bidirectional handshake.

## Root cause confirmed in source
`DiagnosticRX::diagnostic_handshake_start()` required the lifetime counter
`_errors == 0`. Once `DiagnosticRX::poll()` incremented this counter, a
framing error from long before the authorization blocked all future HSLab
leases for that boot. `AP_SpektrumSmartESC_Diagnostics::update_reader()`
also faulted a session whenever `receive_errors() != 0` rather than
examining **new** errors within that lease.

## Change
- Adds a pure C++ policy class `AP_SpektrumSmartESC_UartRecovery`.
- Permits a new, explicitly authorized LAB handshake after **500 ms** since
  the last **framing-only (FE)** event, provided every accumulated error event
  is one of those framing-only events.
- Rejects error histories including overrun, noise, parity, RX-ring overflow,
  GPIO/mux misconfiguration, UART release or any other unclassified increment.
- Does not reset or erase any UART error counter.
- Retains existing RX FIFO empty, BUSY, line-level, PA0 AF8 open-drain, HDSEL,
  RE/TE, authorization, time window, one-shot, disarmed and arming-inhibition
  requirements.
- Takes a UART error counter baseline **before** lease creation. **Any new**
  error after the baseline aborts the handshake as before.
- Exposes status bit6 `handshake_flags` for historical-error eligibility, and
  emits `SPK15C: old FE settled; count=N` upon successful authorization.
  The boot banner now identifies Phase 15C.

## Tests and limits
- Six C++ host policy tests added in `tests/test_spektrum_uart_recovery.cpp`.
  Standalone execution wired into `tests/run_host_tests.py`.
- Policy checks cover zero-error initialization, 499/500-ms transition,
  repeat FE, incompatible errors, retained baseline and uptime wrap.
- Code-path inspection verifies that the zero-counter gate and absolute-error
  session gate have both been replaced. Other UART guards are unchanged.
- This integration has **not** yet been built as a Pixhawk6C firmware image
  in this session, flashed to a Pixhawk, electrically measured, or validated
  with the specific Avian 130A. A software patch is not a handshake success.

## Review and safe validation
1. Review diff and host/C++ test results. Build the actual
   `Pixhawk6C-SpktSmart-HSLab` ArduPlane image with the correct build
   environment; confirm compiler warnings, sizing and firmware hash.
2. Follow the Phase 15B report for *all* isolated electrical safety
   precautions: motor phases, actuators, MAIN3 and ESC red BEC supply
   disconnected/insulated; Pixhawk on USB; shared GND and isolated AUX7
   SRXL2 only; fan/rotor/propeller removed.
3. Verify HSLab boot banner **SPK15C**. Check
   `SPKT_ENABLE=1`, `SPKT_PROBE=0`, `SPKT_HSID=64`,
   `SPKT_TLM=0`, `SPKT_WAVE=0`, `SPKT_SHADOW=0`.
   Keep arming checks enabled; no motor function is enabled by this patch.
4. The existing Phase 15B single authorized startup experiment is
   `SPKT_LABACK=15`, then within ten seconds `SPKT_HS=2` with the
   separately powered ESC connected within the documented active window.
   Do not attempt it if fresh UART, electrical or configuration errors are
   present. One attempt per reboot.
5. Record `SPK15C`, `SPK15`, `SPKT` messages and DataFlash
   `SPKY/SPKU/SPKM/SPKE/SPKX/SPKW/SPKH`; assess UART baseline,
   QUEUED, TC, RXREST, RX_BYTES, CRC/ID, response and finalization.
6. No Control Data, throttle, reverse, telemetry polling or motor movement
   should be exercised in this phase. Do not claim the ESC works until real
   hardware RX/TX exchange, handshake and subsequent telemetry and actuator
   acceptance are separately verified.

## Rollback
Do not merge this branch until reviewed and validated. Return to the parent
`feature/spktsmart-phase15` firmware and parameters to reproduce the
previous image. The old HSLab remains restricted to a one-shot handshake.
