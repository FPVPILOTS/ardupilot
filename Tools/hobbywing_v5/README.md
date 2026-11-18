# Host reference decoder (NOT flight firmware)

Read-only Python implementation of Rotorflight's Platinum V5 **32-byte telemetry** decoding fields. It cannot connect to or change an ESC and is not integrated into ArduPilot.

From repository root:

```bash
PYTHONPATH=Tools/hobbywing_v5 python3 -m unittest discover -s Tools/hobbywing_v5 -p 'test_*.py' -v
```

Tests use synthetic packets and an independent standard CRC test vector (`123456789` = 0x4B37). Passing these tests **does not** demonstrate compatibility with a physical Platinum 80A V5 or validate serial framing. Next steps: add pinned Rotorflight SITL vectors, raw hardware captures and fuzz/regression coverage.

The parser intentionally calls the RPM output `rpm_field_x10`: do **not** interpret it as mechanical shaft RPM until electrical pole-pair behavior is measured. Current consumption in mAh is absent from the wire frame; no cumulative value is fabricated.

See `libraries/AP_HobbywingV5/` for architecture, test and upstream requirements.
