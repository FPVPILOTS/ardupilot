# ArmedBench ARM-before-link test (13 November 2026)

**Scope:** The separately compiled Pixhawk6C-SpktSmart-ArmedBench-IOInstall
target with opt-in SPKT_MOTOR=1. NEVER for flight. Stock Pixhawk6C,
other TurnLab/AutoLab builds and generic native motor qualification are unchanged.

## Intent

Allow a legitimate ArduPlane ARM state transition with a live, fresh zero
RC demand during native SRXL2 discovery, even before the Avian reports its
0x21 handshake or 0x80 sensor telemetry.

The ARM transition is *monitor-only* when the link is not yet motor-ready.
The motor coordinator continues to send only protocol handshakes during
discovery; a link acquired after the ARM edge continues at CH1 minimum only.
A fresh disarm/re-arm with verified zero RPM/zero throttle telemetry is
required before any nonzero digital throttle can be considered.
The reference CH1 minimum 768 is a **candidate**, not proof of actual motor stop.

## Safety invariants

- SPKT_MOTOR defaults to 0. No ARM on failed motor preparation or missing IO
  ownership; no auto-arm from ARMING_REQUIRE=0.
- Arming exception requires fresh RC, zero throttle, valid control state,
  ground evidence, and live DISCOVERY or IDLE motor session.
- Only the dedicated experimental image has this branch. No PWM fallback.
- During monitor-only ARM, a throttle movement cannot energize the SRXL2
  motor; reverse request and RC/control loss fault the session.
- At most two more timed 0x21 handshakes are permitted after an armed
  no-RX discovery timeout; no fake RX, fake telemetry, or 0xCD qualification.
- Any UART/IO/peer/protocol fault or missing telemetry continues to block
  nonzero motor control. After armed failure, reboot/disarm required.
- Existing source-level CH1 whitelist remains 768..4608, CH7 unavailable.
  Reverse is not enabled by this phase.

## Bench acceptance

Check SPK28 ARM MON1 during ARM-before-link and SPK27 RDY0; CH1 remains
uncommanded until real link. Missing RX leads to at most two finite retries
and then ESC_LOSS, not an assumed link. With valid peer and sensor data,
the monitor latch is only cleared by disarming; normal ARM afterwards
still requires all real motor readiness checks.

A green CI build is not an electrical safety qualification, nor evidence
that the Avian responds on the physical AUX7 half-duplex bus.
