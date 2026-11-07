# Phase 3 — native receive-only SRXL2 diagnostics

2026-11-01, Europe/Amsterdam. AI-assisted implementation on branch
`feature/pixhawk6c-spktsmart`. Base ArduPilot revision:
`dbe792162d06cab66c3475fd5556bf7a120f119e`.
ChibiOS gitlink: `d103983f4362e5506bcf7062ccacf345fc0c0859`.
Waf gitlink: `35eadbb64e2052099a853b571e507c33032b392c`.
Changes remain in the working tree; no Phase 3 commit, push or flash was made.
Existing unrelated modifications, including the previously dirty waf/gsoap
submodules and bootloader-build helper, were preserved. No submodule was edited.

**IMPLEMENTED / SOFTWARE TESTED / BUILD VERIFIED. NOT HARDWARE VERIFIED.**
The firmware has a native UART4 receiver and opt-in vehicle diagnostics.
No physical handshake transmission, digital throttle or reverse is available.

## Native receiver and electrical safety boundary

The MCU and pin mapping are unchanged from Phase 2:

| Item | Evidence / conclusion |
| --- | --- |
| Physical signal | AUX7 = PA0 = UART4_TX AF8, FMU PWM OUT connector pin 8. [Holybro port documentation](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports). |
| One-wire reception | [STM32H743 reference manual RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): UART4 supports single-wire half-duplex; HDSEL internally joins receive and transmit paths on the TX pin. PA1/UART4_RX is not required in that mode. |
| Transmitter control | RE and TE are separate enables. The new backend never enables TE and never writes TDR. It uses HDSEL for reception through PA0. GPIO is open-drain AF8 without a pull-up/pull-down when receiving, and input when stopped. |
| Existing HAL limitation | ChibiOS USARTv3 `hal_serial_lld.c`, `usart_init`, forcibly adds TE to CR1 during `sdStart`. Therefore the ordinary serial startup path was not used and is not modified. Clearing TE after ordinary startup would not prevent a transient. |
| PWM8 | PA1 remains TIM5_CH2/PWM8. UART4 never claims it. TIM5 and its existing DMA allocation remain available to PWM8; this receiver has no DMA or UART IRQ allocation. |
| GPIO ownership | PA0 remains input in hwdef and is removed from the exported GPIO56 table. Scripts/relays cannot accidentally turn the reserved AUX7 pin into an output. It is also absent from PWM and SERIALn tables. |
| Physical board | Revision remains unknown; no known deliberate voltage modification. Public pinout and [Holybro revision-dependent voltage documentation](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod) do not establish buffer direction, impedance or receive propagation on this particular board. |

`AP_HAL_ChibiOS/DiagnosticRX` is a dedicated hardware receiver, not bit-banged
serial. Its startup sequence is:

1. Leave PA0 as GPIO input.
2. Enable/reset only UART4, configure 115200 baud, 8N1, FIFO and HDSEL.
3. Write CR1 with UE+RE+FIFOEN, without TE or interrupt enables.
4. Read back UE/RE/TE and HDSEL. On failure, disable UART4 and retain input mode.
5. Only then connect PA0 to AF8/open-drain, with no pull resistor.

A compile-time assertion prohibits TE in the startup control word. Its UART
write implementation always returns zero. Linked Thumb disassembly confirms
`movs r0,#0; bx lr`. The backend is only reachable via the HAL's new optional
`diagnostic_rx_uart()` accessor; other HALs return null. Compile-time guards
reject concurrent UART4 use by ChibiOS serial/UART/SIO drivers.

This establishes a software no-transmit policy and documented MCU capability.
It does **not** establish electrical safety at the external connector. An
unknown unidirectional/level-shifting board circuit could prevent reception or
drive the external net independently of the MCU mode. Do not connect an ESC
until its actual signal path and voltage compatibility are established.

## Receive acquisition and limitations

The backend drains the H7's 16-byte hardware FIFO from ArduPilot's 1-kHz timer
process. A bounded 512-entry ring stores each byte with its acquisition time in
32-bit boot milliseconds. ChibiOS critical sections protect the ring, receiver
state and error counter. Byte framing is performed by hardware; the timer only
services the FIFO. The vehicle task drains at most 512 bytes per call, requested
at 100 Hz through the common AP_Vehicle scheduler, subject to vehicle loop rate.

CRC, length validation, buffering, device discovery observation, invalid-packet
rejection and communication expiry reuse the existing protocol engine. Hardware
framing/noise/parity/overrun errors and software ring overflow increment an error
counter. An error resets observation and discards the buffered stream before
any new publication. Old queued data is evaluated using acquisition timestamps,
not its later consumption time. A new discovery context is required after loss.

Supported diagnostic baud is **115200 only**. A final master handshake agreeing
on 400000 or an unknown rate clears observation and is rejected; there is no
automatic baud switching or PWM fallback. FIFO service latency, CPU cost, GPIO
receive propagation and real inter-packet timing still require hardware capture
under load. Millisecond timestamps cannot prove sub-millisecond bus timing.
The 512-entry ring occupies 4096 bytes and is finite: an excessively delayed
vehicle task can overflow it. Detected loss withdraws measurements; it does not
command a motor or bypass any safety check.

## Passive bus observation

An observer cannot assume telemetry is addressed to FC ID 0x30. In passive mode
the engine now:

- Observes an expected ESC-class source 0x40 handshake and its UID/capabilities.
- Learns the bus-master destination from a CRC-valid final handshake broadcast
  by a receiver/remote/FC-class source.
- Watches CRC-valid channel/control frames for a telemetry poll requesting 0x40.
- Accepts at most one 0x20 ESC sensor reply addressed to that observed master,
  within a conservative 5-ms diagnostic response window.
- Withdraws the snapshot on an observed failsafe frame, communication expiry,
  stream error or missing supported values. A new unrelated poll closes the
  previous reply opportunity.

The 5-ms window is this diagnostic implementation's conservative filter, not a
claim about every Avian firmware's required deadline. There is no source ID in
the SRXL2 telemetry envelope; attribution relies on the protocol's single-master,
unique-device-ID assumptions and observed polling. It is not authentication or
exact model identification. UID/device class is not a firmware-version query.

An ESC alone may announce its handshake but normally needs a master to request
telemetry. This passive firmware does not manufacture those requests. Captured
or bench traffic from an existing compatible master can be observed after
electrical validation; a receiver is not proposed as an installed dependency of
the final direct-controller design.

The [official Rev K specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf)
is retained in workspace `sources/Spektrum-SRXL2/`, SHA-256
`7d9c54de2c1eba38ebcf17eb2f21b8e2f1f35172ad383652bacd827f38e041d5`.
Its 3.3-V/high-impedance electrical requirements remain the hardware gate. Exact
SPMXAE1130A behavior and newer FC-master library behavior remain unvalidated.

## Vehicle integration and configuration

Shared AP_Vehicle integration covers Plane and Copter without editing their
throttle, motor mixing, arming, disarm, failsafe or reverse logic. The feature is
compiled only into the custom board target. Its group uses previously unused
AP_Vehicle parameter index 33; existing group indices were not changed.

| Parameter | Default | Behavior |
| --- | --- | --- |
| `SPKT_ENABLE` | 0 | 1 starts the reserved RX-only backend during disarmed startup. Reboot required to enable/re-enable. Setting 0 stops it and restores PA0 input mode. |
| `SPKT_TLM` | 0 | Optional publication of fresh complete measured voltage/current/FET-temperature to fixed ESC telemetry index 14, displayed as ESC 15. Reboot required. |

Slot 15 is a fixed diagnostic telemetry slot, not automatic motor-output mapping;
it remains fixed if BRD_IO_ENABLE changes output numbering. No other backend
may use that index. An occupied index at startup is refused. Later conflicting
telemetry count or RPM activity latches a conflict and stops touching that slot.
This is defensive conflict detection, not a general atomic ownership registry
for all existing ArduPilot backends. Keep other producers off the reserved slot.

No serial protocol number, SERIALn port or TX parameter was added. Standard
Pixhawk6C compiles neither the native receiver nor the vehicle diagnostic class;
this was checked in the linked ELF. AUX1 retains its previous input reservation.
AUX2–6, AUX8, MAIN/IOMCU and receiver input definitions retain their existing
configuration. Actual output waveforms and receiver operation were not hardware
tested. Other vehicle builds such as Rover were not run.

## Telemetry and diagnostics

`AP_SpektrumSmartESC_Receive` connects timestamped input to the protocol engine
and the existing `AP_SpektrumSmartESC_Telem` adapter. Publication occurs only
when a new, fresh, complete supported sensor snapshot was accepted. Repeated
vehicle updates do not refresh an old reading. Missing temperature/current,
invalid destination, no poll, late reply and invalid CRC do not create data.

The new `AP_ESC_Telem::invalidate_telem_data()` lets the exclusively owning
backend immediately withdraw its snapshot on loss, rather than waiting for the
frontend's generic five-second expiry. Invalid supported fields are cleared;
their type bits and validity are removed. New telemetry from the observer must
not be used as a communication-health or arming-authorisation check.

Measurements follow existing ESC MAVLink groups, ESC battery monitoring and ESC
DataFlash logging. No new sensor message subsystem was added. Electrical RPM is
not converted to mechanical RPM without motor-pole information; BEC temperature
is not relabeled as motor temperature; consumed capacity/errors are not invented.
Live Mission Planner display or physical ESC DataFlash records are not verified.

Diagnostic status changes use GCS STATUSTEXT. A one-Hz `SPKT` DataFlash record
contains TimeUS, State, Dev, UID, Bytes, HWErr, CRC, Len, Partial, Reject, Unk, HS,
Poll, Tlm and LastRx. HWErr is an acquisition-error counter, not an ESC-reported
hardware fault. LastRx is acquisition milliseconds. Status 1 means listening,
2 an observed device, 3 a fresh decoded ESC sensor frame (not necessarily a
complete publishable snapshot), and 4 a telemetry-producer conflict. No arming
readiness is implied. Normal logging requirements apply; bench/disarmed logging
requires the normal LOG_DISARMED setting. It was not changed automatically.

## Handshake preparation without physical transmission

`AP_SpektrumSmartESC_Handshake` is an offline packet planner with no HAL, UART,
GPIO or write callback. Controlled-time mock exchanges test:

- Disabled state and startup listen interval.
- Unicast ESC discovery/handshake construction, frame bounds and CRC.
- Expected source/destination and baud-capability filtering.
- Final 115200-baud broadcast after the expected reply.
- Bounded retries, timeout, wraparound and yielding to an observed lower-ID master.

The planner returns bytes only into caller-owned test buffers. It is not invoked
by diagnostics and is absent from the linked Plane ELF. It constructs no control,
throttle, reverse or parameter-write packets. It is preparation for later work,
not a complete multi-device master scheduler or proof of Avian FC compatibility.

## Actual validation results

Docker/WSL2 environment reused from Phase 2: official CI image
`ardupilot/ardupilot-dev-chibios:v0.1.3`, digest
`sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`.
Ubuntu 22.04.3, GNU Arm Embedded 10.2.1, Python 3.10.12. Only the isolated Linux
snapshot had line endings normalized. The Windows checkout was not rewritten.

| Check | Actual result |
| --- | --- |
| Protocol, observer, mock handshake and frontend GTests | 30 PASS |
| Timestamped transport / real frontend GTests | 7 PASS |
| Standalone protocol/planner tests | 29 PASS, warnings as errors |
| ASan + UBSan protocol/planner tests | 29 PASS in final run |
| Actual board resource checks | 6 PASS |
| Generated UART4/AUX7 resource candidate checks | 6 PASS |
| Parameter metadata | Official Plane and Copter parser both exit 0; both new names emitted |
| Linked firmware inspection | RX/diagnostics present in custom ELF, absent in stock; offline planner absent; RX write stub returns zero |

The 7 transport tests cover refusal of writable/already-owned ports, no-write
operation, acquisition-time preservation across slow updates, stale backlog,
publication exactly once, timeout/recovery, stream errors, missing-field
withdrawal and conflicting producer handling. No native MCU code was executed
on the host. GTests are host integration tests, not vehicle SITL flight tests.

An initial sanitizer run using the image's default PIE layout entered repeated
`AddressSanitizer:DEADLYSIGNAL` handling and was stopped. Its cause is not proven;
it is retained in attempt logs, not counted as passing. Final tests used
`-fno-pie -no-pie`, a 30-second timeout, and
`ASAN_OPTIONS=abort_on_error=1:handle_segv=0`; address/undefined-behavior
instrumentation remained enabled, with libasan/libubsan recorded. A segfault or
sanitizer failure still fails the command. No host ASLR/BIOS setting was changed.
These sanitizers cover the hardware-independent protocol/planner, not the H7
register backend. FIFO/timer timing remains a hardware validation item.

### Final firmware builds

All final configure/build exit codes are zero, with zero compiler warning lines.

| Target | Vehicle | Flash used | Free flash | Data+BSS |
| --- | --- | ---: | ---: | ---: |
| Pixhawk6C-SpktSmart | Plane | 1,590,812 B | 375,256 B | 145,816 B |
| Pixhawk6C-SpktSmart | Copter | 1,594,644 B | 371,428 B | 146,904 B |
| Pixhawk6C | Plane | 1,586,640 B | 379,432 B | 141,684 B |
| Pixhawk6C | Copter | 1,590,424 B | 375,644 B | 142,780 B |

Data+BSS is static RAM only, not runtime peak heap/stack use. Build summaries and
the artifact manifest are authoritative for exact sizes. APJs, BINs, ELFs and
SHA-256 manifests are collected in workspace:

- `custom-firmware/Pixhawk6C-SpktSmart-phase3-artifacts/`
- `custom-firmware/Pixhawk6C-phase3-artifacts/`

Collector checks APJ board ID 56, decompressed image length and exact BIN payload
equality. Phase 2 artifacts were not overwritten. Final logs are in
`logs/spktsmart-phase3-linux/`; intermediate failures are preserved in
`logs/spktsmart-phase3-attempt1` through `attempt4`.

### Reproduction

Host-independent protocol tests and generated resource checks, from source root:

```sh
python libraries/AP_SpektrumSmartESC/tests/run_host_tests.py --arm-cxx arm-none-eabi-g++
python libraries/AP_SpektrumSmartESC/tests/verify_hwdef.py --board Pixhawk6C
python libraries/AP_SpektrumSmartESC/tests/verify_aux7_candidate.py
```

The Docker helper `tools/validate-spktsmart-phase3.sh` runs all four normal waf
firmware builds, both GTest executables and the bounded sanitizer run. It reuses
the tested Linux source snapshot under `/build/src`; new sources must be
explicitly synchronized or a fresh snapshot volume used. There is no upload or
flash command. `tools/inspect_spktsmart_phase3.py` verifies linked symbols and
the actual write stub. `tools/collect_spktsmart_artifacts.py --phase 3` collects
validated outputs without replacing Phase 2 artifacts.

## Modified code and remaining boundary

- Native platform backend: `AP_HAL_ChibiOS/DiagnosticRX.h`, `.cpp`; HAL class
  accessor; `AP_HAL/HAL.h` and `UARTDriver.h` optional receive-only APIs.
- Shared library: receiver, diagnostics/configuration and offline handshake
  planner; protocol passive-observer extension; existing telemetry adapter reused.
- `AP_Vehicle` registration/init/scheduler; common library list; ESC frontend
  invalidation API. No vehicle motor-control code was changed.
- Custom hwdef reserves PA0 exclusively and enables compile support, with runtime
  diagnostics disabled by default. Standard Pixhawk6C hwdef is unchanged.
- Protocol/transport tests, resource verifier, build/inspection/artifact helpers,
  this report and the target README.

**Stop at the physical validation boundary:** identify PCB revision and the
PA0-to-connector buffer circuit; establish receive direction, idle impedance,
signal voltage and absence of contention before connecting or driving the ESC.
Then measure startup/disable, FIFO overrun behavior and timestamp latency under
load, along with PWM8 and receiver input waveforms. Capture actual Avian discovery
and telemetry; verify exact target model/firmware separately.

AUX7 is MCU-capable but not electrically proven. If its physical circuit proves
unusable, other AUX AFs were checked in Phase 2: no independent alternative TX
pin without additional resource conflicts was established. TELEM3/USART2 (stock
SERIAL5, PD5 TX / PA3 RX) is the preferred existing-port fallback to investigate:
unlike TELEM1/2 it is not a stock bootloader UART. Its TX circuit would still need
the same bidirectional/voltage verification. Holybro's TELEM3 pin-4/5 revision
warning remains applicable; no wiring or fallback activation is authorised here.

No converter, external MCU, installed receiver dependency or hardware change was
introduced. No physical ESC communication, motor failsafe, reverse, flight or
flight-readiness claim is made. Actual transmission remains unavailable.
