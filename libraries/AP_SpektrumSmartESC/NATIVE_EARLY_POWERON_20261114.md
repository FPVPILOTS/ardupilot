# Experimental Pixhawk6C EarlyNative startup

This is a separate, **non-armable, non-flight** firmware target:
`Pixhawk6C-SpktSmart-EarlyNative-IOInstall`. It is not an alteration
of the existing ArmedBench installer and is not production motor firmware.

## Physical fault being isolated
Real Avian 130A Ver A bench tests demonstrated valid `0x40 -> 0x30`
handshake and `0x80` replies with Pixhawk powered *before* ESC,
but frequently RX0 when both were powered by one battery.

The earlier 50ms fix measured 50ms AFTER native reader start, not
50ms after the flight controller powered on. Ordinary native driver
startup happens near the end of `AP_Vehicle::setup()`.

## Change
- Initialize the **same native session** immediately after
  `BoardConfig.init()` with saved `SPKT_MOTOR=1`, `SPKT_ENABLE=0`.
- Run a bounded 180ms early pump and later service it in the existing
  scheduler delay callback while the rest of vehicle setup completes.
- No new/second UART owner, no artificial handshake success, no reset
  of a valid link on completion of `AP_Vehicle::setup()`.
- Permit a temporary **disarmed-only** IOMCU inhibit-readback delay:
  retry acquiring the real IO lease after 100ms, without acquiring
  UART4 or transmitting anything before proof.
- `SPK33 BOOT INIT... NOW...` reports native initialization time
  relative to FC power-on to contrast with `FIRST50ms`.
- This target explicitly blocks the ArmedBench ARM-before-link
  exception, even with throttle zero. No automatic ARM.
- Initial SRXL2 handshake remains at 115200 baud. The official Spektrum
  code supports negotiating 400000 when both peers advertise support.
  **This patch deliberately does not falsely claim to switch baud**:
  the current hardware RX is 1kHz-polled and must be redesigned and
  validated before a real 400k physical switch is enabled.
- The existing ArmedBench source uses CH1=768 as its unqualified neutral
  candidate. Never connect ESC motor phases when flashing or testing
  this target. Even an unarmed link can transmit a digital zero candidate.

## Acceptance
Build special IO first; build new target; verify board ID 56 and
`SPK33` string. Bench: all motor phases disconnected and individually
insulated, throttle at zero, DISARMED, failsafe intact. First reconnect
Mission Planner after one normal shared-battery startup and compare
`SPK33 BOOT INIT`, `SPK29 FIRST`, `SPK27 RX/RP` and `SPK30 PU`.

A host/mock PASS cannot prove actual boot timing. If INIT is still
after the ESC window and RX remains zero, we need a genuinely earlier
MCU/IO bootstrap or demonstrated electrical power sequencing. The
goal is normal power-on operation, not a manual reconnect ritual.
