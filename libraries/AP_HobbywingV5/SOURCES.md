# Primary sources and provenance (compiled 2026-11-18)

## Rotorflight (protocol authority for our initial port)

- Rotorflight firmware `src/main/sensors/esc_sensor.c` (inspected source commit `2a0a8d10712499651f00a817af5559bab6582b87`):
  https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/main/sensors/esc_sensor.c
  Relevant functions: `pl5DecodeTeleFrame`, `pl5Decode`, `pl5Accept`, `pl5SensorInit`, `calculateCRC16_MODBUS`, `rrfsmSensorProcess`.
- Rotorflight simulator `src/utils/sitl/esc/pl5_esc.py` (same source commit):
  https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/utils/sitl/esc/pl5_esc.py
  Explicitly models what Rotorflight firmware expects; **not evidence of physical ESC behavior**.
- Wire simulator `esc_wire.py`:
  https://github.com/rotorflight/rotorflight-firmware/blob/2a0a8d10712499651f00a817af5559bab6582b87/src/utils/sitl/esc/esc_wire.py
- Rotorflight ESC setup guide:
  https://doc.rotorflight.org/stable/setup/esc-telemetry/

## ArduPilot integration and constraints

- AP_ESC_Telem backend interface:
  https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_ESC_Telem/AP_ESC_Telem_Backend.h
  Provides `update_rpm()`, `update_telem_data()`; current standard fields do not include BEC voltage/current.
- AP_BLHeli serial telemetry consumer:
  https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_BLHeli/AP_BLHeli.cpp
  Can claim `SerialProtocol_ESCTelemetry` UART — requires exclusive routing.
- ArduPilot serial ESC telemetry user docs:
  https://ardupilot.org/copter/docs/common-esc-telemetry.html

## Vendor / other integrations (cross-check, not exact hardware proof)

- Spirit Hobbywing V5 telemetry:
  https://manual.spirit-system.com/index.php/HobbyWingV5_Telemetry
- Hobbywing Platinum family product information:
  https://www.hobbywing.com/

## Required still to collect

1. Official **Platinum 80A V5 exact submodel** hardware pinout, feature matrix and firmware revision.
2. Official Pixhawk 4 **TELEM1 connector orientation/pinout and IO ratings**.
3. Captured valid raw serial frames from user's ESC with CRC and physical reference measurements.
4. Rotorflight licensing/copyright headers and dependency audit for any eventual source translation.
5. ArduPilot upstream contribution and documentation requirements at PR time.
6. Confirmation whether BEC voltage/current/temp and motor temp are genuinely populated on this ESC.
7. Independent motor pole count and mechanical gear ratio, current-monitor calibration.
8. Specific ArduPilot fork base SHA and test-toolchain versions, build and flash evidence.

No claim of electrical compatibility or live data decoding is made until these are verified.
