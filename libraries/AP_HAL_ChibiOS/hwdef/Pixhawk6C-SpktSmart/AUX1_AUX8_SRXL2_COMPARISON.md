# Pixhawk6C AUX1–AUX8 native SRXL2 interface comparison

2026-11-01. Read-only hardware/software feasibility audit of the existing
Pixhawk6C-SpktSmart project, base ArduPilot revision
`dbe792162d06cab66c3475fd5556bf7a120f119e`.

**Recommendation: keep AUX7/PA0/UART4 as the primary PWM-output candidate.**
**Second PWM candidate: AUX8/PA1 using UART4 HDSEL plus TX/RX SWAP.**
**Existing UART fallback: TELEM3/USART2, subject to its own electrical review.**
No output is electrically qualified for the Avian SPMXAE1130A. Physical TX,
throttle, reverse and handshake transmission remain disabled. No migration,
firmware source/configuration change, flash or electrical test was performed.

## 1. Two corrections to earlier exclusions

1. A pin labeled UART RX is **not automatically excluded** from one-wire use.
   STM32H7 supports `USART_CR2.SWAP`. With `USART_CR3.HDSEL`, the combined
   transmit/receive signal can be considered on the normally RX-mapped pad.
   This makes PA1/UART4_RX and PA8/UART7_RX conditional native candidates.
   Earlier phase notes that dismissed AUX8 solely because it is UART4_RX were
   incomplete. SWAP is peripheral signal routing; the GPIO still uses the RX
   pad's documented AF, not a fictional new UART_TX alternate-function entry.
2. The absence of a per-pin `BIDIR` annotation is **not proof of absent bdshot
   software support**. `bdshot_setup_group_ic_DMA()` also uses paired capture
   channels (CH1/CH2 or CH3/CH4). Thus the official definition's four explicit
   capture-DMA declarations provide driver routes for the paired outputs too.
   These routes are software evidence, not physical tests on all eight pins.

Primary evidence for SWAP is ST's
[H7 USART low-layer driver](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Inc/stm32h7xx_ll_usart.h):
`LL_USART_SetTXRXSwap()` modifies CR2.SWAP;
`LL_USART_ConfigHalfDuplexMode()` configures HDSEL without clearing SWAP.
The local ArduPilot `UARTDriver.cpp:1591–1592` explicitly swaps effective line
identities, `:1628` sets SWAP, and `:1674` independently enables half-duplex for
H7. The swapped one-wire alternatives below are a source-supported MCU routing
assessment. This audit does not claim that they were bench-tested or that a
particular board buffer supports the resulting direction/idle behavior.
The vendored ST device header
`modules/ChibiOS/os/common/ext/ST/STM32H7xx/stm32h743xx.h:26419` lists UART4,
UART7 and USART2 in `IS_UART_HALFDUPLEX_INSTANCE`; this independently confirms
the relevant peripherals have native half-duplex capability.

## 2. Timer groups: functional banks versus generated indices

For this comparison, groups 1–3 mean the three AUX banks requested by the user:

| Functional group | Outputs | Peripheral | Common PWM timer properties |
| --- | --- | --- | --- |
| 1 | AUX1–AUX4 | TIM1, CH1–CH4 | Shared counter, period, clock and timer-update DMA; not four independent PWM-rate domains. |
| 2 | AUX5–AUX6 | TIM4, CH3–CH4 | Shared timer/update DMA. |
| 3 | AUX7–AUX8 | TIM5, CH1–CH2 | Shared timer/update DMA. |

Do not equate these labels with `HAL_PWM_GROUP1/2/3`. The retained stock generated
definitions order groups TIM5, TIM4, TIM1; the bdshot snapshot orders TIM1, TIM5,
TIM4 because resource resolution changes ordering. Firmware must identify a
group by timer/driver and original PWM numbers, not a presumed generated index.
The user-facing AUX names here are FMU-local outputs, not SRXL2 channel numbers
or an automatic assignment of a vehicle throttle/motor function.

## 3. Complete per-output mapping

All eight MCU pads support GPIO digital input/output and timer input/output
modes. That does not mean their external connector paths are bidirectional.
GPIO numbers below are the stock hwdef mappings; the custom target currently
reserves AUX1 as input/GPIO50 and AUX7 as input without exported GPIO56.

`U` in the final column means **UNVERIFIED external buffer, input tolerance,
signal voltage, impedance and bus-release behavior**, assessed separately for
that particular output. Nominal AUX logic is documented as 3.3 V; no row is an
electrical GO. FMU PWM OUT pin 10 is ground; signal pin numbers are included.

| AUX / group | MCU / stock GPIO / connector signal | Timer AF | All UART/USART AF functions on this pad | Native one-wire UART route | Official bdshot capture route | Electrical |
| --- | --- | --- | --- | --- | --- | --- |
| AUX1 / 1 | PA8 / GPIO50 / pin 2 | TIM1_CH1 AF1 | UART7_RX AF11; USART1_CK AF7 | UART7 HDSEL + SWAP; **UART7 already owns TELEM1/SERIAL1** | Explicit BIDIR; TIM1_CH1 DMA | U |
| AUX2 / 1 | PE11 / GPIO51 / pin 3 | TIM1_CH2 AF1 | None | No hardware UART data signal on this pad | Paired input through TIM1_CH1 DMA | U |
| AUX3 / 1 | PE13 / GPIO52 / pin 4 | TIM1_CH3 AF1 | None | No hardware UART data signal on this pad | Explicit BIDIR; TIM1_CH3 DMA | U |
| AUX4 / 1 | PE14 / GPIO53 / pin 5 | TIM1_CH4 AF1 | None | No hardware UART data signal on this pad | Paired input through TIM1_CH3 DMA | U |
| AUX5 / 2 | PD14 / GPIO54 / pin 6 | TIM4_CH3 AF2 | UART8_CTS AF8 | CTS is flow control, not TX/RX; HDSEL/SWAP do not turn CTS into data | Explicit BIDIR; TIM4_CH3 DMA | U |
| AUX6 / 2 | PD15 / GPIO55 / pin 7 | TIM4_CH4 AF2 | UART8_RTS AF8 | RTS is flow control, not TX/RX; HDSEL/SWAP do not turn RTS into data | Paired input through TIM4_CH3 DMA | U |
| AUX7 / 3 | PA0 / GPIO56 / pin 8 | TIM5_CH1 AF2 | UART4_TX AF8; USART2_CTS_NSS AF7 | **UART4 HDSEL, SWAP=0**; already reserved by this project | Explicit BIDIR; TIM5_CH1 DMA | U |
| AUX8 / 3 | PA1 / GPIO57 / pin 9 | TIM5_CH2 AF2 | UART4_RX AF8; USART2_RTS AF7 | **UART4 HDSEL + SWAP**; alternative to AUX7, not a second independent UART4 bus | Paired input through TIM5_CH1 DMA | U |

USART1_CK on PA8 is a synchronous clock, not asynchronous serial data.
Likewise NSS/CTS/RTS functions do not provide an SRXL2 byte stream. Selecting a
different timer group cannot add a UART alternate function to PE11/PE13/PE14
or convert UART8 flow control into UART8_TX/RX. Their other SPI/FMC/display
functions are not native asynchronous UART substitutes.

The map is extracted from `Pixhawk6C/hwdef.dat:114–121` and
`hwdef/scripts/STM32H743xx.py`. The latter is the repository's datasheet-derived
AF table; ST's [STM32H743 datasheet](https://www.st.com.cn/resource/en/datasheet/stm32h743vg.pdf)
is the external pin-function reference. Exact source hashes and the extracted
eight-row inventory are in workspace `sources/aux-output-audit/inventory.json`.

## 4. GPIO, timer/DMA conflicts and neighboring-output impact

| AUX | GPIO / ownership needed for SRXL2 | Timer/DMA and impact on neighbors |
| --- | --- | --- |
| AUX1 | Input/output-capable PA8; currently reserved in custom target. A UART7 experiment must exclusively take UART7 from TELEM1 and prevent ordinary serial startup on it. | Remove only TIM1_CH1 from PWM; AUX2–4 can retain TIM1 and their numbering with a regenerated hwdef. Do not stop/reset TIM1 or commandeer paired TIM1_CH1 capture DMA from active bdshot. New UART7 IRQ/clock/ownership work is required. |
| AUX2 | Input/output-capable PE11; remove PWM/GPIO51 ownership if repurposed. No UART route available. | Timer-assisted serial would require a new deterministic implementation and a TIM1/capture/DMA ownership audit affecting AUX1–4. Existing paired capture uses CH1 resources; this is not an independent native UART. Not recommended. |
| AUX3 | Input/output-capable PE13; remove PWM/GPIO52 ownership if repurposed. No UART route available. | TIM1_CH3 capture is shared with AUX4's paired route and the TIM1 update transaction. A custom timer serial implementation could disturb the whole TIM1 bank; not recommended. |
| AUX4 | Input/output-capable PE14; remove PWM/GPIO53 ownership if repurposed. No UART route available. | Paired capture uses TIM1_CH3. Same TIM1-bank limitations as AUX3; not recommended. |
| AUX5 | Input/output-capable PD14; UART8_CTS does not qualify as serial input data. | TIM4_CH3 capture is shared with AUX6; custom timer serial would require exclusive scheduling and review of both outputs. No native UART candidate. |
| AUX6 | Input/output-capable PD15; UART8_RTS does not qualify as serial output data. | Paired TIM4_CH3 capture and shared TIM4 update resources; same restrictions as AUX5. |
| AUX7 | Input/output-capable PA0; already excluded from PWM/GPIO/SERIALn ownership. Current RX/bench and gated port target it. | UART4 uses neither TIM5 nor timer DMA. AUX8/PA1 can remain PWM8, as checked in earlier phases. Do not use bdshot's whole-timer capture switch to implement UART direction changes. Current prepared UART TX uses FIFO/TC IRQ, not DMA. |
| AUX8 | Input/output-capable PA1; currently active PWM8/GPIO57 must be removed in a separate reviewed variant if selected. | Same UART4 clock/IRQ as AUX7, but SWAP and pin checks must change. PA0 could be restored as PWM7 at reboot after its old receiver is fully detached. TIM5 stays available for PWM7. AUX7 and AUX8 cannot independently host two HDSEL UART4 sessions. |

The official bdshot driver first allocates directly declared capture DMA, then
uses the paired channel if a selected output has no direct capture stream
(`RCOutput_bdshot.cpp:78`, especially `:143–176`). Input capture can select the
partner's input through the timer's direct/indirect input mapping
(`bdshot_config_icu_dshot`, `:388`). One shared DMA stream is time-multiplexed;
the pairing is not simultaneous independent telemetry. Output/capture switching
also resets shared timer configuration, so bdshot support is not permission to
reuse that timer while normal neighboring outputs run without review.

### Retained DMA allocation snapshots

These are actual generated files from earlier phases, **not fixed MCU stream
requirements**. DMA resolution depends on hwdef/resource changes. No generation
or firmware build was needed for this documentation audit.

| Timer / bank | Stock generated update stream and sharing | Official bdshot generated update/capture streams |
| --- | --- | --- |
| TIM1 / AUX1–4 | DMA2 stream 4, shared with SPI1_RX and SPI2_TX | DMA2 stream 3: TIM1_UP, TIM1_CH1 and TIM1_CH3 share the stream with switched DMAMUX requests |
| TIM4 / AUX5–6 | DMA2 stream 3, shared with USART1_TX and SPI1_TX | DMA2 stream 6: TIM4_UP and TIM4_CH3 |
| TIM5 / AUX7–8 | DMA2 stream 2, shared with USART2_TX | DMA2 stream 5: TIM5_UP and TIM5_CH1 |

Sources: repository `build/spektrum-smart-hwdef/Pixhawk6C/hwdef.h` and workspace
`logs/spktsmart-phase5-linux/bdshot-generated-hwdef.h`. Allocations in the custom
target need not equal either snapshot. Reassess them if a future design adds
UART DMA. The present AUX7 native receiver/prepared diagnostic transmitter do
not allocate DMA. ArduPilot's ordinary H7 UART option handling also disables
RX/TX DMA for half-duplex (`UARTDriver.cpp:1685–1691`) to avoid timing effects on
other devices sharing streams. None of these software mechanisms validates an
external electrical signal path.

## 5. Electrical limitations apply to every row

The [Holybro port page](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
documents nominal AUX signal voltage and connector mapping. Its
[revision-dependent voltage modification page](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod)
distinguishes RC09/RC12 voltage selection from RC13, which does not offer AUX
5-V modification. The user's actual revision and solder-pad state are unknown.
These documents do not establish a per-output bidirectional buffer circuit.

For **each** AUX1–AUX8, still unknown are the buffer/translator/protection part,
whether external signals reach the MCU, whether MCU input or open-drain mode
actually releases the connector, power-off injection behavior, effective pull-up
and rise time. Do not assume that all outputs share one voltage selector or an
identical buffer path; equally, there is no evidence that changing from bank 3
to bank 1 or 2 fixes a circuit limitation. Timer groups describe MCU scheduling,
not PCB buffer directions or a guarantee of electrical isolation.

The [official SRXL2 electrical specification](https://github.com/SpektrumRC/SRXL2/blob/master/Docs/SRXL2%20Specification.pdf)
uses a 3.3-V idle-high data signal with released/high-impedance idle. Servo-bus
power is a separate voltage. A 3.3-V push-pull PWM output or successful bdshot
capture does not prove safe open-drain UART idle/reception. Moving to SWAP does
not change those external constraints. No MCU 5-V-tolerance label establishes
the connector's weakest component rating.

Thus all eight electrical verdicts remain **UNVERIFIED**. Apply the existing
[Phase 6 qualification procedure](PHASE6_HARDWARE_VALIDATION.md) separately to
any alternative pad before connecting an ESC or injecting test signals. No
direct-wire design, pull resistor or additional electronics is declared necessary
or sufficient without that evidence.

## 6. Ranked recommendation

The ranking is for **native UART feasibility and software integration risk**.
It is not a ranking of measured electrical safety; no output has passed that
gate. Effort is relative to the existing project, not a time/cost estimate.

| Rank | Candidate | Native feasibility / effort | Safety and functionality tradeoff |
| --- | --- | --- | --- |
| 1 | **AUX7 / PA0 / UART4_TX AF8** | Direct HDSEL, no SWAP; lowest additional effort. Existing startup, receive diagnostics, exclusive ownership, prepared TC release, watchdog and bench arming veto already apply. | No existing TELEM UART taken over; preserves AUX8 PWM8. Actual board signal path still unverified. **Recommended primary.** |
| 2 | **AUX8 / PA1 / UART4_RX AF8 with SWAP** | Source-supported native alternative; medium effort for pin descriptor, SWAP initialization/guards, ownership, hwdef changes and tests. | No extra UART sacrificed; can preserve/restore AUX7 PWM7. Not electrically better on current evidence, and no swapped-pin bench evidence exists for this board. First alternative if PA1's actual circuit proves superior. |
| 3 | **AUX1 / PA8 / UART7_RX AF11 with SWAP** | Native conditional alternative; higher effort for another peripheral, IRQ/clock guards and UART7 takeover. | TELEM1/SERIAL1 cannot keep its normal concurrent function. Stock bootloader also lists UART7; startup/bootloader ownership needs review. No established electrical advantage over PA0/PA1. |
| 4= | AUX2 / PE11 | No UART data AF; would require a separate timer/DMA serial project. | TIM1-bank scheduling/capture effects and fresh timing proof required. Not a recommended native UART migration. |
| 4= | AUX3 / PE13 | Same exclusion: no UART data AF. | Explicit bdshot DMA does not fix missing UART framing/peripheral mapping. |
| 4= | AUX4 / PE14 | Same exclusion: no UART data AF. | Paired capture is not a UART resource. |
| 4= | AUX5 / PD14 | CTS-only, no UART data AF. | Neither timer regrouping nor SWAP converts CTS into data. |
| 4= | AUX6 / PD15 | RTS-only, no UART data AF. | Neither timer regrouping nor SWAP converts RTS into data. |

No alternative is currently better overall than AUX7. Consequently **no driver
migration is recommended or made now**. Timer-assisted UART might be researched
separately, but it is not an existing native UART path, not validated by DShot's
GCR decoder and not a justification for unproven bit-banged motor control.

## 7. Conditional migration plan without discarding work

If electrical measurements establish that PA1 is usable and PA0 is not:

1. Preserve the protocol engine, device observation, handshake/mock scheduler,
   telemetry adapter, calibration/command simulations, diagnostics, parameter
   indices, bench arming/emergency-stop latch and hardware-TX gate.
2. Factor the HAL's hardcoded PA0 details into one compile-time board transport
   descriptor: GPIO line, AF, register bit positions, expected CR2.SWAP state,
   timer/GPIO exclusion and permitted owner. Reuse the same UART4 receiver/lease,
   FIFO, TC interrupt and watchdog; do not create a second UART4 driver.
3. Initialize CR2.SWAP with UE clear, keep TE clear before mux connection, use
   PA1's AF8 open-drain mapping and update all input/AF/OTYPER guards, status
   sampling, abort, shutdown and teardown paths. Include CR2 in bench evidence
   so SWAP can be verified. Verify effective RX on PA1;
   changing only the pin name would leave the current configuration wrong.
4. Make a separate reviewed reboot-only hwdef selection. Remove PA1 PWM8 and
   GPIO57 ownership. Restore PA0 PWM7 only after it has no native owner, preserving
   original PWM numbering and TIM5's remaining channel mapping. No live
   PWM/digital switching and no automatic PWM fallback.
5. Update only affected pin/resource tests and add SWAP/startup/release/ownership
   tests. Build affected variants, then repeat electrical qualification for PA1.
   Keep physical TX disabled throughout software development.

For AUX1, the same transport/protocol architecture can be reused, but UART7
must be exclusively reserved outside the ordinary TELEM1 driver, with correct
IRQ/clock/peripheral guards and bootloader review. Unmux the original PE7/PE8
UART7 data pads (and review flow-control pads) before enabling SWAP; leaving
multiple pads assigned to the same effective data function can propagate output
onto an unintended connector. USART1_CK must not be used as
a substitute UART data function. AUX2–4 should keep their original numbering;
their timer must not be reset by the UART backend. This is a larger fallback
migration, justified only by independently better electrical evidence.

The current AUX1 reservation is historical. Restoring its PWM functionality can
be considered separately after the chosen transport is qualified; it is not
silently changed by this audit.

## 8. Existing UART fallback: TELEM3 / USART2

Prefer TELEM3 if no AUX pad can be qualified, provided its current user function
can be released. Stock ordering makes USART2 **SERIAL5**, with PD5/USART2_TX AF7
and PA3/USART2_RX AF7. The documented connector is:

| TELEM3 pin | Function |
| --- | --- |
| 1 | +5 V supply, not a 3.3-V signal pin |
| 2 | USART2_TX, nominal 3.3-V logic; primary HDSEL pad candidate |
| 3 | USART2_RX, nominal 3.3-V logic; SWAP alternative only after its own path review |
| 6 | GND |
| 4, 5 | Usually unconnected; on specified serial-number revisions they are I2C4. Do not commandeer them. |

PD5 can host one-wire HDSEL without using PA3. **Do not short connector TX and
RX together.** A dedicated serial connector still does not prove that its TX
buffer accepts incoming signals or releases safely. Both the chosen signal path
and power plan require qualification. PA3/SWAP is another MCU routing option,
not proof that an input-only external buffer can transmit.

TELEM3 is preferable to taking TELEM1/TELEM2 because it preserves those commonly
used ports, does not take receiver/GPS pins, and USART2 is not in the stock
bootloader's `SERIAL_ORDER` (UART7/UART5/USART3 are). This is an ownership/startup
advantage, not evidence that the pin is electrically quiet in every power state.
Actual user wiring/protocol configuration is unknown; SERIAL5 must not remain
assigned to another device. A future fallback must reuse the exclusive transport
contract and bench gates, not start the ordinary UART driver and then clear TE
after a possible startup transient. GPIO/UART RX buffers and level shifters need
the same scrutiny as PWM outputs.

Stock DMA resolution shares USART2_TX with TIM5_UP. A FIFO/interrupt half-duplex
backend, like the prepared UART4 path, avoids taking that DMA stream; a DMA-based
fallback would require a fresh shared-resource audit. The no-external-receiver/
converter objective remains possible in principle, not established physically.

## 9. Audit evidence and retained safety boundary

`tools/audit_spktsmart_aux_outputs.py` reads the existing stock/bdshot AF maps,
extracts all eight outputs and saves `sources/aux-output-audit/inventory.json`
with source hashes. Its successful inventory identifies one direct and two
SWAP-based hardware UART data candidates. It verifies the existing hardware TX
gate is still zero. This is a source audit, not a hardware test.

No firmware C++/parameter/hwdef file was modified. No build, protocol-test or
resource-check matrix was repeated for documentation-only changes. Earlier
Phase 6 build/test artifacts remain the current software evidence. A direct
download of the large ST reference-manual PDF did not complete and was stopped;
this audit relies on the cited ST driver/datasheet, the existing reference-manual
research and actual repository code, not a newly inspected PDF or forum claims.

Final decision: **AUX7 recommended; AUX8 is a real conditional native alternative;
TELEM3/USART2 is the existing UART fallback. All physical interfaces remain
UNVERIFIED and physical TX remains NO-GO until Phase 6 electrical qualification.**
