# Hobbywing Platinum V5 serial ESC telemetry for ArduPilot

**Status: research / port preparation; no hardware-approved driver yet.**

## Scope

Native, read-only ESC telemetry for Hobbywing Platinum V5 family, initially **Platinum 80A V5** on a **Blade 500X / Holybro Pixhawk 4 / ArduCopter**. Potential later validation: Platinum 150A V5.1 / Pixhawk 6C. **No Hobbywing DataLink V2. No ESC programming, configuration writes, reset commands or motor control via the telemetry interface.**

Goal: contribute a small, maintainable, documented ArduPilot implementation that can be proposed upstream after review, testing and hardware evidence. This directory is a project staging area, **not a built or enabled ArduPilot driver**.

## Current known setup (operator-reported, not independently verified)

- Physical port: Pixhawk 4 **TELEM1**, corresponding to SERIAL1 on the current configuration.
- SERIAL1_PROTOCOL=16 (ESC Telemetry), SERIAL1_BAUD=115 (115200 baud).
- Stock ArduPilot Mission Planner fields still zero; this does **not** prove a wiring fault.
- Telemetry signal measured using multimeter around 3.0–3.3 V; dynamic levels, pinout and output electrical type **not yet verified**.
- Existing independent Align rotor RPM sensor must remain functional.
- ESC throttle control stays on its existing normal PWM/control output; driver is observation-only.

## Project documents

- [PROTOCOL.md](PROTOCOL.md): traced Rotorflight PL5 packet format and validation rules.
- [ARCHITECTURE.md](ARCHITECTURE.md): intended ArduPilot integration, port ownership and telemetry mapping.
- [BENCH_VALIDATION.md](BENCH_VALIDATION.md): bring-up sequence, safety controls and evidence capture.
- [UPSTREAM_PLAN.md](UPSTREAM_PLAN.md): quality gates and proposed submission strategy.
- [SOURCES.md](SOURCES.md): primary sources, provenance and outstanding questions.

## Source attribution and licensing

Rotorflight and ArduPilot are open source. Reusing/translating portions of Rotorflight's GPL-covered implementation requires compliance with the applicable license conditions and preserving attribution/copyright. This directory records implementation ideas and protocol facts; it does **not** claim Rotorflight code has yet been ported.

## Milestones

1. Protocol evidence and frozen reference fixtures.
2. Host-side read-only 32-byte decoder with CRC, fuzz/truncation tests and simulator vectors.
3. Exclusive serial-port selection, receive-only parsing and robust stale-data handling in ArduPilot.
4. AP_ESC_Telem RPM/voltage/current/temperature integration; verify physical vs electrical RPM and motor-pole scaling.
5. Add documented BEC and fault telemetry without misrepresenting standard ESC_Telemetry fields.
6. SITL + Pixhawk 4 dry bench evidence; later independent airworthiness/review gates.
7. Upstream-focused PR(s) to ArduPilot developers; no guarantee of acceptance or stable release.

**Never flash experimental firmware for flight before the documented checks have passed.**
