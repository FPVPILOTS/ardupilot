# Phase 2 evidence — Pixhawk6C-SpktSmart

2026-10-31, AI-assisted development. Branch `feature/pixhawk6c-spktsmart`.
Base revision `dbe792162d06cab66c3475fd5556bf7a120f119e`, with existing local
changes retained. No flashing, ESC connection, motor operation or flight testing.

**BUILD VERIFIED does not mean HARDWARE VERIFIED.** These APJs are development
artifacts with no active Smart transport, throttle or reverse implementation.

## Firmware compilation resolved

Native Windows failed in ChibiOS build rules requiring POSIX `touch` and shell
environment assignments. MSYS also encountered unquoted paths containing spaces.
The Linux image initially exposed Windows CRLF shebangs (`python3\r`).

Docker Desktop's existing Linux engine runs under WSL2; only the `docker-desktop`
WSL distribution is installed. No BIOS, firmware, WSL distribution or Windows
feature settings were changed. Instead, the repository's own
`.github/workflows/test_chibios.yml` CI image was used:

`ardupilot/ardupilot-dev-chibios:v0.1.3`

Resolved digest:
`sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`

Measured environment: Ubuntu 22.04.3 LTS, Python 3.10.12, GNU Arm Embedded
10-2020-q4-major / GCC 10.2.1. The mounted Windows source was read-only. An
isolated snapshot in Docker volume `spktsmart-phase2-work`, `/build/src`, was
normalized to LF. Existing generated builds and unused gsoap sources were
excluded. Submodule auto-updates were disabled; no submodule was modified.

The final source snapshot includes the AUX7 input reservation. All four firmware
builds configured, compiled and linked successfully:

| Target | Vehicle | Flash used | Free flash | Data + BSS | Compiler warning lines |
| --- | --- | ---: | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | Plane | 1,586,612 B | 379,456 B | 141,688 B | 0 |
| Pixhawk6C-SpktSmart | Copter | 1,590,396 B | 375,676 B | 142,776 B | 0 |
| Pixhawk6C | Plane | 1,586,556 B | 379,512 B | 141,688 B | 0 |
| Pixhawk6C | Copter | 1,590,340 B | 375,732 B | 142,776 B | 0 |

Data+BSS is static RAM, not measured runtime heap/stack use. The custom builds use
56 bytes more flash than their respective regression controls. This is a compile
regression control, not proof of unchanged physical outputs or flight behavior.
Rover/other vehicle builds have not been run.

APJ, BIN and ELF artifacts are copied to workspace directories:

- `custom-firmware/Pixhawk6C-SpktSmart-phase2-artifacts/`
- `custom-firmware/Pixhawk6C-phase2-artifacts/`

Complete final build/configuration logs, environment, exit-code CSV and SHA-256
artifact manifest are in `logs/spktsmart-phase2-linux/`. The ELF is named
`arduplane.elf` / `arducopter.elf` in the collected directory; waf originally
names these executables without a suffix. APJ board ID is 56.

Reproduction helpers in workspace `tools/`:

- `build-spktsmart-linux.sh`: snapshot, official compiler PATH, normal waf
  configure and Plane/Copter builds for both boards, artifact collection.
- `normalize-spktsmart-snapshot.py`: refuses to operate outside `/build/src`;
  normalizes selected text files only in the container copy.
- `collect_spktsmart_artifacts.py`: requires successful log evidence, collects
  actual outputs, sizes, warning counts and hashes.

For a fresh source snapshot, use a new Docker volume and a new container name.
The existing volume intentionally retains the tested source and build products.
The helper reuses a marked existing snapshot; source changes must be explicitly
synchronized or a fresh volume used. It never builds against the Windows path.

## AUX7: what is confirmed

| Question | Evidence / conclusion |
| --- | --- |
| Exact MCU pin | PA0, GPIO56, stock TIM5_CH1 / PWM(7). `Pixhawk6C/hwdef.dat`. |
| Physical connector | FMU PWM OUT / AUX OUT, connector pin 8 = FMU_CH7. Pin 9 is FMU_CH8, pin 10 ground, pin 1 VDD_Servo. [Holybro pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports). |
| UART alternate function | PA0 UART4_TX AF8; stock PWM uses AF2. `scripts/STM32H743xx.py`. |
| Other PA0 functions | TIM2_CH1/ETR AF1, TIM8_ETR AF3, TIM15_BKIN AF4, USART2_CTS_NSS AF7, SDMMC2_CMD AF9, SAI2_SD_B AF10, ETH_MII_CRS AF11, EVENT-OUT AF15 in the repository pin table. None is selected concurrently. |
| Hardware half-duplex | RM0433's USART/UART feature table explicitly lists single-wire half-duplex for UART4/5/7/8. HDSEL internally connects TX and RX; the separate RX pin is unused. See [ST reference manual](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) and its [USART half-duplex excerpt](https://community.st.com/ysqtg83639/attachments/ysqtg83639/stm32-mcu-products-forum/63172/3/STM32Ref.pdf). |
| PWM8 conflict | PA1 is TIM5_CH2 / PWM8 and optionally UART4_RX AF8. Hardware half-duplex does not need PA1 as UART RX. The generated candidate leaves PA1 on TIM5_CH2 with its original PWM8 index. |
| Timer/DMA | Stock TIM5_UP shares DMA2 stream 2 with USART2_TX in the generated map. PWM8 still needs TIM5. The candidate disables only channel 1 of that timer group and gives UART4 no DMA allocation. |
| Existing serial ports | UART4 is unused by stock SERIAL_ORDER. The candidate appends it after existing user serial ports without changing those indices; the internal IOMCU entry moves after it as generated normally. |

The uninstalled resource candidate is generated by
`libraries/AP_SpektrumSmartESC/tests/verify_aux7_candidate.py`. Its pin declaration
is `PA0 UART4_TX UART4 NODMA OPENDRAIN PULLUP`; it does not allocate UART4_RX.
Six checks pass, including open-drain/pull-up generation and retained PWM8.
This is a pin/resource audit, not a runnable or electrically validated transport.
No UART4 definition was added to the actual firmware target.

## Electrical and timing limitations

The official [Spektrum SRXL2 specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf)
was successfully downloaded and parsed locally: Rev K, 20 pages. Local copy:
`sources/Spektrum-SRXL2/specification.pdf`, with extracted text alongside it.

Section 5 specifies 3.3-V signaling and a normally high-impedance bus with a weak
pull-up. Sections 6 and 7 specify 115200/400000 baud, 8N1, inter-packet spacing,
startup discovery and communication-loss behavior. A 3.3-V connector label alone
does not prove receive direction, impedance or safe bus sharing.

The user's board revision is unknown, with no known deliberate signal-voltage
modification. Holybro documents revision-dependent PWM voltage options, including
different RC09/RC12/RC13 behavior. Its public pinout does not identify all buffers
or prove that connector pin 8 is electrically connected bidirectionally to PA0.
See [Holybro voltage modification documentation](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod).
No modification is proposed or performed here.

`AP_HAL_ChibiOS/UARTDriver.cpp` supports HDSEL and disables DMA in half-duplex.
It temporarily clears HDSEL for TX, then restores it after both transmission-end
and output-empty events, serviced by its 1-kHz thread. This is not proof of SRXL2
turnaround under flight load. Minimum two-character bus spacing corresponds to
about 174 us at 115200 or 50 us at 400000; idle release and the available reply
window must be measured. GPIO output type must also be explicitly controlled:
the UART option helper's `set_pushpull` name refers to pull resistors, not proof
that it configures the pin as open-drain.

**Transport decision: BLOCKED by electrical verification, not by missing MCU
UART support.** AUX7 remains the primary candidate. Other AUX mappings were
checked: AUX1 exposes UART7_RX with an existing TELEM1 ownership conflict;
AUX2–4 lack UART data AFs; AUX5/6 expose UART8 flow control, not TX/RX; AUX8 is
UART4_RX and can remain PWM8. No TELEM fallback or bit-banged motor path was
selected. No converter or external MCU is used.

The actual target reserves both AUX1 and AUX7 as inputs, with GPIO50/GPIO56.
AUX2–6, AUX8 and MAIN outputs keep conventional functionality. Explicit GPIO
users can still claim those GPIO numbers; do not assign them during experiments.
No automatic PWM/digital transition or PWM fallback is implemented.

## Protocol and telemetry implementation

IMPLEMENTED / TESTED:

- Existing bounded passive framing, CRC, discovery observation, channel/failsafe
  decoding and sensor 0x20 decoding retained.
- ESC device class and configured expected ID filtering; handshake priority
  restricted to documented 1–100 and baud capability restricted to 0/1.
- New tests for malformed capabilities, bad traffic not extending freshness,
  unavailable sensor fields and every single-bit payload/CRC corruption.
- `AP_SpektrumSmartESC_Telem` passes complete, fresh voltage/current/FET-temperature
  snapshots to the real `AP_ESC_Telem` frontend, with explicit ESC index bounds
  and safe centi-degree range checks. Partial snapshots are skipped because the
  frontend accumulates field validity with a common expiry.

The publication helper must be called once per newly accepted telemetry frame;
there is no periodic publication task connected to it. The parser's 50-ms observation
expiry and the frontend's existing 5-second telemetry expiry are distinct. The
frontend is not an ESC communication-health or arming check.

The adapter uses the existing path consumed by ESC MAVLink groups, DataFlash
logging and the ESC battery backend. Publication into the frontend is tested;
live MAVLink reception, Mission Planner display and DataFlash records have not
been demonstrated with a physical ESC. No new MAVLink message or parameter was
invented. BEC temperature is not mapped to motor temperature; electrical RPM
is not reported as mechanical RPM without motor-pole information. No consumed
capacity, errors or unsupported sensors are fabricated.

The protocol library and telemetry adapter are **not instantiated by any vehicle**.
The generated firmware currently contains the board reservation and ordinary
ArduPilot functionality, not an operational Smart ESC driver. The adapter was
compiled and linked through real ArduPilot GTest. No serial protocol parameter,
port ownership, motor output integration, master scheduler or reverse command
is active. A 0x4x device class / UID does not establish Avian model or firmware
version; target-model identification and configuration readback remain unresolved.

Rev K describes receiver master selection, while the newer official library
also contains flight-controller master handling. This distinction must be
resolved for a vendor-supported direct-controller implementation; receiver
protocol support alone is not proof of target Avian behavior.

## Validation

| Check | Actual result / evidence |
| --- | --- |
| Real ArduPilot GTest | 20 PASS; `gtest-configure.log`, `gtest-build.log`, `gtest-results.log`. Includes publication to real AP_ESC_Telem, stale rejection, index bounds, missing-temperature and overflow checks. |
| Standalone C++ tests | 19 PASS with warnings as errors, including official ARM compiler object compilation. |
| AddressSanitizer + UBSan | 19 PASS, no reported sanitizer errors; `protocol-sanitizers.log`. |
| Actual board definition | 6 PASS; `logs/spktsmart-phase2-hwdef.log`. |
| Uninstalled UART4/AUX7 resource candidate | 6 PASS; `logs/spktsmart-phase2-aux7-candidate.log`. |
| Python test helper lint | flake8 PASS, 127-character limit; `logs/spktsmart-phase2-python-lint.log`. Python helpers also pass syntax compilation. |
| Four complete firmware builds | All exit 0; `outcomes.csv` and per-board/per-vehicle logs. |
| Physical bus, motors, failsafe, reverse | NOT HARDWARE VERIFIED. No flight-readiness claim. |

The GTest executable is a host integration test, not a vehicle SITL flight test.
No full vehicle autotest or hardware waveform test has been run.

## Modified files

Repository files changed/added during Phase 2:

- `libraries/AP_HAL_ChibiOS/hwdef/Pixhawk6C-SpktSmart/hwdef.dat`, `README.md`, this report.
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC.cpp`.
- `libraries/AP_SpektrumSmartESC/AP_SpektrumSmartESC_Telem.h` and `.cpp`.
- `libraries/AP_SpektrumSmartESC/tests/test_spektrum_smart.cpp`, `verify_hwdef.py`,
  `verify_aux7_candidate.py`.

Workspace helpers, sources, logs and artifacts are described above. Standard
Pixhawk6C definitions, vehicle safety code, RCOutput, SerialManager and receiver
input code were not edited. Existing unrelated modifications were retained.

## Next required hardware validation

1. Establish actual PCB revision and trace the PA0-to-FMU_CH7 path, including
   buffer type/direction and any resistor network. Resolve this before connecting
   or driving the ESC signal. Connector orientation must follow Holybro's drawing.
2. With suitable high-impedance instrumentation and a confirmed electrical path,
   measure idle impedance/voltage, receive propagation, reset/boot behavior and
   UART release timing. Neither BEC voltage nor servo power belongs on the signal.
3. Capture a known-compatible ESC exchange and confirm SPMXAE1130A firmware,
   device behavior, throttle endpoints, telemetry polling, loss-of-link response
   and configured reverse channel. Do not probe using guessed motor commands.
4. Only after those results, implement exclusive UART4 ownership and the native
   half-duplex transport, then deterministic scheduling and per-vehicle arming,
   disarm/failsafe/watchdog tests. Plane and Copter need distinct output-safety
   integration. Keep the motor mechanically safe and propeller/EDF load removed
   for any later powered tests.

No external converter is part of the proposed installed design. Measurement
equipment used for validation is not a proposed runtime dependency.
