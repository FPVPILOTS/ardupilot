# Hardware inventory, references, issues and decision log

## Target machine (to validate)

- Flight controller: Pixhawk 4, Traditional Heli firmware (installed version previously reported as ArduCopter 4.5.7; verify current version in vehicle log).
- Platform: Blade 500 traditional helicopter.
- Swashplate: first priority three-servo configuration, actual geometry and motor-to-output mapping to be captured from .param.
- Reported swash servo: KST SV915-12 (confirm exact printed label and quantity); tail KST model to be verified.
- Electric signal neutral: user reports all four servos are 760us devices. Confirm each model against manufacturer datasheet.
- Intended refresh: 560Hz, but verify output protocol, supported PWM rate and shared hardware timer banks.
- Intended servo supply: 8.4V regulated; exact BEC model, maximum *continuous/peak* current, wiring topology, cable gauge and grounds to be verified. Never assume Pixhawk 4 signal rail provides 8.4V.

## External references / hierarchy of evidence

1. Exact current source tree and build SHA (ground truth for firmware behavior).
2. Physical servo model name and manufacturer PDF; verify nominal pulse, min/max accepted high time, operating voltage, signal-high and refresh before permitting hardware.
3. Autopilot board and I/O specifications, output-timer grouping and rail power documentation.
4. ArduPilot Traditional Heli setup: https://ardupilot.org/copter/docs/traditional-helicopter-swashplate-setup.html
5. Official TradHeli parameter list: https://ardupilot.org/copter/docs/traditional-helicopter-parameter-list.html
6. Actual .param, RCOU logs, measured PWM waveform and blade-pitch gauge data.

## Important version distinction

The KST development branch was created from `FPVPILOTS/ardupilot:master`. This is **not necessarily identical** to the user's installed 4.5.7 firmware or another local custom branch. Parameter compatibility, board target and defaults must be reviewed prior to flash.

## Engineering risks / open issues

| ID | Priority | Issue | Resolution evidence |
|---|---|---|---|
| R-01 | BLOCKER | Existing code has never been compiled after edits | Copter SITL/target build PASS |
| R-02 | BLOCKER | Unverified 560Hz timing + shared timer groups | scope/logic analyzer or equivalent electrical capture |
| R-03 | BLOCKER | Exact tail servo spec unavailable | manufacturer datasheet for exact model |
| R-04 | BLOCKER | Unknown BEC power/current and servo rail | wiring diagram and datasheet, bench voltage under load |
| R-05 | HIGH | H4, H1 and dual swash missing pre-arm coverage | tests plus code validation |
| R-06 | HIGH | Baseline master != installed firmware | before/after param diff and rollback plan |
| R-07 | HIGH | Endpoints may be mechanically too large | measured binding-free blade/linkage motion |
| R-08 | HIGH | Swash centroid vs zero-thrust vs trim not calibrated | H_SV_MAN 2/3/4 blade pitch measurements |
| R-09 | HIGH | RPM sensor/receiver/ESC share physical wiring & ground considerations | power/ground topology review |
| R-10 | MED | UI shows parameter range from metadata, actual runtime validation differs | parameter write/readback across reboot |
| R-11 | HIGH | Invalid servo endpoints while armed or post-change | runtime guard/failsafe and unit tests |
| R-12 | HIGH | Incorrect assumptions about servo channels = output 1,2,3 | SERVOx_FUNCTION map from actual vehicle |

## Decisions recorded

- D-001: Opt-in helicopter output mode `H_SW_PWM_MODE`, legacy default 0.
- D-002: Keep `H_COL_*` in existing internal control domain; convert at servo output stage, not by globally scaling all helicopter parameters.
- D-003: Keep frequency validation and analog pulse-range support as independent concerns.
- D-004: Keep external gyro, RSC and ESC signal paths unchanged in phase 1.
- D-005: Branch is a development experiment until compile, SITL and hardware gates pass.

## Change-request evidence expected from user

Export a full parameter file from Mission Planner, with firmware version/board, send exact model identifiers for all 4 servos and BEC, and provide intended rotor geometry (H3/H4). Do not request unnecessary personal information.
