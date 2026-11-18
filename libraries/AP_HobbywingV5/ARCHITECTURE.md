# ArduPilot integration design (proposal)

## Design objective

Small, general-purpose **Hobbywing Platinum V5 serial telemetry backend**, not a Blade-500X-specific fork. Separate wire parser (unit-testable) from ArduPilot UART lifecycle and ESC telemetry publication.

Data path:

```text
Platinum 80A V5 P+GND [electrical verification required]
    -> Pixhawk 4 TELEM1 UART RX (SERIAL1)
    -> HobbywingV5 read-only receive/parser
    -> AP_ESC_Telem (standard supported measurements) / optional battery monitor integration
    -> MAVLink ESC telemetry and onboard dataflash
    -> Mission Planner
    -> separate BEC/fault telemetry logging/transport after interface review
```

ESC throttle remains controlled by existing ArduCopter output. The existing independent rotor sensor is not replaced or reconfigured.

## Serial ownership (blocking design issue)

Current operator parameters: `SERIAL1_PROTOCOL=16`, `SERIAL1_BAUD=115`. In ArduPilot, `AP_BLHeli` can locate `SerialProtocol_ESCTelemetry`; careless reuse would allow two consumers to read the same UART and corrupt framing. Before implementation, inspect all call sites and decide one ArduPilot-maintainer-acceptable path:

- Extend the established ESC-telemetry dispatcher with **explicit Hobbywing V5 type selection**, ensuring exactly one consumer; or
- introduce an explicitly named, registered protocol type with independently owned UART while retaining compatibility.

Do not claim `SERIAL1_PROTOCOL=16` alone switches a stock firmware to Hobbywing V5. No undocumented numeric parameters. Preserve other ESC telemetry options and do not regress BLHeli support.

## Proposed software responsibility boundaries

- Wire parser: `parse_telemetry_frame(bytes,len,TelemetrySample&)` with CRC check and typed fields, no HAL dependency.
- Serial backend: control UART ownership, per-byte framing, synchronization, 115200 baud, timestamping, timeout and counters. No writes in phase 1.
- AP_ESC_Telem adapter: call `update_rpm` and `update_telem_data` with exact presence masks. Determine electrical vs mechanical RPM and motor poles **before publishing**.
- Extended data: BEC voltage/current/temperature and fault field need appropriate log/MAVLink design. Existing `AP_ESC_Telem_Backend::TelemetryData` has motor and ESC temperatures, but no native BEC fields in the inspected version. Do not mislabel BEC current as motor current or invent a battery monitor.
- Consumption: integrate valid current over elapsed time only after deciding what energy domain is represented. No synthetic data from stale frames.

## Robustness requirements

- CRC and length reject, resynchronization, exception-safe parsing, no heap allocations on hot receive path.
- timeout freshness tracked separately from numeric value. Disconnected ESC should show invalid/stale, not reassuring `0` readings.
- optionally suppress repeated status chatter; report counters (frames, CRC fails, framing fails, timeouts).
- never send throttle/programming commands or ESC resets.
- no prearm bypass, no arming/motor-safety changes.
- compile only when feature enabled; minimize flash/RAM overhead and guard by HAL feature defines.
- keep optional code usable on other boards and ArduPilot vehicle types.

## Open decisions before implementation

- Verify fork's current ArduPilot base vs upstream master; APIs vary.
- Choose serial config pathway and avoid protocol-16 collisions.
- Confirm exact TELEM1 connector pin numbering against official Pixhawk 4 manufacturer manual before energizing.
- Verify idle polarity, high levels, electrical type, common ground and whether P is passive-transmit vs half-duplex.
- Test motor pole conversion and gear ratio against independent rotor-RPM sensor.
- Decide log field naming, MAVLink transport and compatibility with Mission Planner; not all fields automatically appear in the Status tab.
- Validate whether 80A supports each BEC/motor temperature field with non-placeholder readings.
