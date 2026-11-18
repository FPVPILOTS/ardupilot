# Fork-to-upstream compatibility audit (2026-11-18)

**Blocking finding:** this FPVPILOTS/ardupilot fork has an older ESC telemetry API than currently inspected ArduPilot upstream master. Do not assume the master API when coding against this fork.

| API | FPVPILOTS/ardupilot master inspected | Current ArduPilot/ardupilot master inspected |
|---|---|---|
| `AP_ESC_Telem_Backend::update_rpm` | `(uint8_t esc_index, uint16_t new_rpm, float error_rate)` | `(uint8_t esc_index, float new_rpm, float error_rate)` |
| `TelemetryData` | ESC temp, voltage, current, consumption, usage, motor temp, freshness | richer optional extended telemetry and newer API |
| `AP_SerialManager::SerialProtocol_ESCTelemetry` | enum value 16 | must recheck during native integration |
| ESC UART use | existing `AP_BLHeli` read path can claim ESC telemetry UART | same concern; API/lifecycle may have changed |

**Design consequence:** `Protocol.h` is intentionally a **HAL-independent C++11 frame decoder**. Test this independently, then add a carefully selected ArduPilot adapter against the chosen upstream base. Do not simultaneously integrate into this fork's legacy API and assert upstream compatibility.

Existing fork references:
- `libraries/AP_ESC_Telem/AP_ESC_Telem_Backend.h`
- `libraries/AP_SerialManager/AP_SerialManager.h`
- `libraries/AP_BLHeli/AP_BLHeli.cpp`

Upstream counterpart:
- https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_ESC_Telem/AP_ESC_Telem_Backend.h

## Pending prerequisite

1. Identify exact fork default branch commit and its divergence from upstream.
2. Choose whether to create a clean branch from updated upstream master or keep a legacy compatibility adapter. Prefer a clean upstream-derived feature branch for official PRs.
3. Confirm AP_BLHeli serial consumption, set exclusive ownership, and document how standard `SERIALx_PROTOCOL=16` is mapped/configured.
4. Check board-specific compile flags, memory footprint and CRC dependency choices.
5. Only then add the UART backend and AP_ESC_Telem publishing; no experimental port ownership in main until resolved.

## Current code

- `Protocol.h`: no HAL or firmware version dependencies, telemetry-only.
- `Tools/hobbywing_v5/test_protocol.cpp`: standalone C++11 host test.
- `Tools/hobbywing_v5/decoder.py`: independent Python research decoder.

Host compilation:

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  Tools/hobbywing_v5/test_protocol.cpp -o /tmp/test_hobbywing_v5
/tmp/test_hobbywing_v5
```

Local research environment on 2026-11-18: g++ with the command above **PASS**; additional ASAN+UBSAN run **PASS**. These are host unit checks only, not ArduPilot firmware build or hardware proof.

## Additional implementation checks

- Verify telem frame bytes 6–8 and allowed header values against physical capture.
- The 16-bit RPM field's interpretation remains unconfirmed for user's motor; no direct publication to governor yet.
- BEC fields may be populated, constant, unavailable or from an internal regulator that does not power the user's servos.
- Some versions of Mission Planner may not display custom BEC data automatically; separately design documented MAVLink/log support.
