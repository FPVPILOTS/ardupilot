# Phase 8: MAIN3 feasibility and unified logical Smart ESC control

2026-11-01, Europe/Amsterdam. Continue existing work on
`feature/pixhawk6c-spktsmart`, base commit
`dbe792162d06cab66c3475fd5556bf7a120f119e`. AI-assisted, uncommitted changes.
No hardware, IOMCU image, throttle parameter or physical output was changed.

**Decision:** MAIN3 is **NO-GO as a hardware-UART SRXL2 interface** on its mapped
STM32F103 pin. A new timer/DMA serial implementation is theoretically possible
but **UNVERIFIED**, with significant IOMCU scheduling, MAIN4 and failsafe work.
It is not currently a safe replacement for AUX7. **Keep AUX7/UART4 as the
recommended native digital candidate**, still electrically unqualified.

The desired physical MAIN3 connection cannot be retained by changing SRXL2
channel numbers or selecting a different FMU UART. Firmware cannot move PA0's
UART signal onto the physically wired IOMCU PB8 pad. No wiring migration is made
here. MAIN3 remains conventional PWM; physical Smart throttle/reverse stay off.

## 1. Exact MAIN3 mapping and IOMCU capabilities

[Holybro's specification](https://docs.holybro.com/autopilot/pixhawk-6c/technical-specification)
identifies the IO processor as STM32F103. More specifically, the actual Pixhawk6C
hwdef embeds `iofirmware_f103_lowpolh.bin` and
`iofirmware_f103_dshot_lowpolh.bin` (`Pixhawk6C/hwdef.dat:260,266`). The F103
targets inherit `iomcu/hwdef.inc`; the generic `iomcu/hwdef.dat` is F100 and must
not be mistaken for this board's selected F103 image.

| Item | Source-verified result / physical limitation |
| --- | --- |
| MAIN3 | IOMCU **PB8 / TIM4_CH3 / PWM(3) / GPIO103**, `iomcu/hwdef.inc:40`. I/O PWM OUT pin 4 is IO_CH3; pin 10 is ground in the official connector mapping. Existing three-pin adapters must be mapped separately. |
| MAIN4 | PB9 / TIM4_CH4 / PWM(4) / GPIO104, same timer. |
| Native UART | PB8 has no USART TX or RX function on the expected F103x8/xB. TIM4_CH3 and remapped I2C1_SCL/CAN_RX functions do not provide UART data. F1 uses AFIO remapping, not H743 AF8 selection. |
| USART remapping | USART1 TX/RX are PA9/PA10 or PB6/PB7; other USART mappings also do not place data on PB8. H7 SWAP does not create an absent F103 UART function. |
| GPIO / capture | PB8 supports MCU GPIO input/output and TIM4_CH3 input capture. Connector input tolerance/release and actual buffer path are not established. |
| Existing bdshot | `iomcu-f103-dshot/hwdef.inc:19` explicitly marks PB8 BIDIR UP_SHARED. This is intended timer-capture return-path support, not a UART/SRXL2 implementation or a measurement on this PCB revision. |
| Capture DMA | F1 DMA1 channel 5 for TIM4_CH3. The DMA map also assigns USART1_RX to that channel, so any future DMA use must respect the selected receiver-driver configuration. |
| Output DMA | TIM4_UP uses DMA1 channel 7, shared with high-speed FMU-link USART2_TX and TIM2_CH2. Existing dshot code orders transactions explicitly to share it. |
| Timer availability | F103xB has four timers. Dshot IO uses TIM1 for the system timer; TIM2/3/4 serve the MAIN banks. Normal IO also has receiver timing requirements. There is no demonstrated unused timer/resource budget to assume. |
| MAIN4 effect | Current IO dshot hwdef explicitly says mixed outputs on a timer are unsupported. Reconfiguring TIM4's counter/PSC/ARR/capture for MAIN3 can alter MAIN4. A new mixed serial/PWM driver cannot simply reuse whole-timer bdshot switching. |
| Electrical levels | MAIN outputs are nominally 3.3 V, with revision-dependent level selection; RC13 permits MAIN 5-V modification even though AUX 5-V modification is removed. Actual revision, solder state, buffer direction and protection remain unknown. |

Pin/remap evidence: [ST F103x8/xB datasheet](https://www.st.com/resource/en/datasheet/stm32f103vb.pdf)
and [RM0008 AFIO remapping](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-microcontrollers-stmicroelectronics.pdf).
Electrical reference: [Holybro ports](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports)
and [voltage-selection guidance](https://docs.holybro.com/autopilot/pixhawk-6c/pwm-signal-voltage-mod).
No actual package marking, netlist or MAIN3 electrical measurement was supplied.
The source mapping and absent UART are established; board-level compatibility
is not. No revision is declared electrically safe merely from its name.

### Timer serial alternatives

A purpose-built timer output-compare/DMA encoder plus edge-capture asynchronous
8N1 decoder could be investigated. It would need byte framing, start/stop-bit
validation, accurate 115200 timing, turnaround and CRC-validated packet assembly.
DShot's pulse/GCR decoder does not supply those functions.

Possible architectures include mixed TIM4 channel operation without changing
MAIN4's waveform, or another timer triggering GPIO DMA writes while TIM4 captures
PB8. Neither is implemented/proven, and the latter still needs a timer and fixed
F1 DMA-channel ownership solution. Sacrificing MAIN4 or removing receiver support
is not an acceptable silent workaround. The dshot IO variant undefines the PPM
input configuration when reallocating timing resources; adopting it just for
capture does not prove receiver functionality is preserved for every setup.
Unproven bit-banged transmission or main-loop sampling at UART bit timing is
not implemented.

## 2. FMU–IOMCU link and required IO changes

The firmware maps FMU H743 USART6 (PC6/PC7) to the IO link, and IOMCU F103 USART2
(PA2/PA3) to the FMU. IO `uart_cfg` runs at **1.5 Mbit/s**, separate from an ESC
bus at 115200. These USART2 pins are on the IO processor, not the FMU's TELEM3
USART2. Internal communication is not an unused external serial port.

`ioprotocol.h` defines register-page RPC with 8-bit IO CRC, maximum 22 16-bit
registers per packet and protocol versions 4/10. Current pages cover PWM,
failsafe, mixing, GPIO, DShot and DShot telemetry; there is no negotiated Smart
ESC ownership/command/status service. SRXL2's CRC16 is a separate validation layer.
The presence of an RC input protocol named SRXL does not make IO an SRXL2 ESC
bus master.

For option A, a new, separately reviewed IOMCU image and FMU protocol extension
would need all of the following before physical operation:

1. Permanently/safely reserve MAIN3 from startup through boot/reset, PWM, GPIO,
   mixer/failsafe and direct-output paths. No early PWM and no PWM fallback after
   digital selection. Bootloader/power-state behavior needs its own review.
2. Prove deterministic timer/DMA serial TX/RX under simultaneous 1.5-Mbit/s FMU
   traffic, RC reception, MAIN4/other outputs, heater/safety and interrupt load.
   Do not forward individual serial bytes through an FMU RPC loop. Timing and
   scheduling must be owned locally by the IO transmitter/receiver.
3. Add version/capability negotiation, atomic route ownership and inhibit ACK,
   command generation/sequence, bounded command age, arming/safety context,
   explicit stop/loss behavior and independently checked mode transitions.
   No legacy IO image may accept a Smart mode it does not implement.
4. Return CRC-valid telemetry with sample identity and acquisition age, errors,
   configuration and timeout status. Repeated reads must not refresh old data.
   Account for different IO/FMU clocks rather than comparing raw boot times.
5. Prevent all other producers from re-enabling MAIN3, including local mixing
   and GPIO writes, while a digital lease exists. Preserve MAIN4 and RC inputs.
6. Revalidate safety button/mask/arming behavior and IO watchdog/reset recovery.
   Existing IO logic detects FMU silence after **200 ms** and fills failsafe PWM
   at roughly 100 Hz (`iofirmware.cpp:583`, `fill_failsafe_pwm`). It may run the
   local mixer. That PWM behavior cannot be reused as a digital stop command.
   ESC stop and communication-loss semantics must be independently validated;
   ceasing frames is not assumed to stop a motor immediately.

The configured F103 target budget is 64 KiB flash with a 4-KiB bootloader reserve
and 20 KiB RAM. Existing inherited BIN lengths are 40,656 B (normal) and 53,396 B
(dshot). These are file sizes, not a RAM/free-stack or new-feature fit estimate.
The latter leaves only 8,044 B within the configured 60-KiB application partition
before any new implementation. No custom IO firmware was compiled or claimed to
fit. The actual board's chip marking/capacity is not inferred from this budget.

**No IOMCU firmware change is made.** Both inherited BIN files match their Git
HEAD contents; hashes and source references are recorded by
`tools/audit_spktsmart_main3.py` in workspace
`sources/main3-audit/inventory.json`. Do not run the IO build/copy tool or replace
ROMFS images just to enable a pin mode: `AP_IOMCU::check_crc()` / the uploader
can program a different embedded IO image during a later FMU boot. Enabling IO
DShot selects a DShot image, not Smart Throttle. No IO upload/flash occurred here.

## 3. Logical channel mapping, independent of the connector

| Domain | This request | Meaning |
| --- | --- | --- |
| ArduPilot function | `SRV_Channel::k_throttle` / function 70 | Calculated throttle, unchanged |
| User's servo assignment | `SERVO3_FUNCTION=70` | Existing logical output assignment; not modified or read live from a controller here |
| Current physical output | MAIN3 = IO PWM3 = PB8 | Conventional PWM until an actual exclusive digital-output integration exists |
| RC input channel 3 | Independently configured RC mapping | Not the source automatically forwarded to the Smart bus |
| SRXL2 throttle | Channel 1, zero-based index 0 | Final controller magnitude encoded using verified ESC calibration |
| SRXL2 reverse | Channel 7, zero-based index 6 | Verified forward/reverse channel level; not physical AUX7 or RC input 7 |
| Two-channel SRXL2 mask | `0x00000041` | Only virtual channels 1 and 7, on one signal wire |

For an AUX7 physical implementation, the signal lead must ultimately move to
AUX7; the logical throttle function need not move to a new controller calculation.
There is no firmware setting that redirects the external MAIN3 connector to
PA0. A future backend could retain SERVO3's logical assignment while routing
function 70 digitally, **only after actually suppressing its legacy physical
PWM output**. No such interception/muting is installed by this phase.

## 4. Implemented safe, transport-independent simulation interface

New `AP_SpektrumSmartESC_Control.{h,cpp}` wraps the existing tested Commands
engine without HAL, serial, IOMCU or vehicle writes. It fixes the requested
virtual channel indexes to 0/6, regardless of a caller's servo/RC numbering.
The generic Commands engine remains reusable for other profiles/models; only a
read-only direction-state getter was added.

The new API is deliberately named `select_simulated` / `prepare_simulated`:

- Route qualification defaults false. Selection is disarmed-only and rejects
  MAIN3 unconditionally because there is no implemented IO serial transport.
- AUX7 selection is a simulation of a future qualified route, not UART activation.
  No new user parameter or unsupported selectable hardware option is registered.
- Frame preparation requires fresh, epoch-matched ownership evidence, exclusive
  routing, MAIN3 PWM inhibited and AUX7 PWM reserved. Missing/stale/conflicting
  evidence disables the simulated route; it never restores legacy PWM automatically.
- Epoch changes prevent acknowledgements from an earlier selection being reused.
  The 50-ms age policy is a simulation contract, not a verified hardware deadline.
- Disarm or throttle suppression clears neutral/direction authorization. RC loss,
  ESC loss, failsafe, invalid values or ownership faults disable the route and
  require explicit disarmed reselection followed by stopped, forward neutral.

**These ownership flags are test inputs, not an implemented IO acknowledgement.**
Current IO enable/disable/channel-mask APIs must not be treated as proof of a
persistent exclusive inhibit through resets/mixing/GPIO paths. A future real
coordinator must implement and verify that reservation before allowing a motor
backend to run. This module emits bytes only into caller-owned test buffers;
it does not mute MAIN3, drive AUX7 or make duplicate physical throttle possible.
Physical digital mode remains unavailable, so legacy SERVO3 behavior is untouched.

### ArduPlane throttle and reverse integration contract

No ArduPlane controller/servo code is changed. The future input is a **final
signed throttle percentage**, after the existing control/suppression/limit logic,
not RC channel 3 or a PWM pulse width. Relevant source paths are
`Plane::set_throttle()` / `set_servos()` in `servos.cpp`, watt/slew/landing/AFS
limits and `reverse_thrust.cpp` (`have_reverse_thrust`, `allow_reverse_thrust`).
The vehicle adapter must honor all of those and actual arming, hardware safety,
emergency stop and manual/passthrough cases. `get_output_scaled(k_throttle)` alone
is not a sufficient safety envelope; forced PWM limits and mode-specific output
paths require review. No live consumer is hooked up in this phase.

The model maps positive demand to forward magnitude. Negative demand or an
explicit zero-throttle reverse request requires both verified ESC settings and
vehicle reverse permission. While changing direction, it emits calibrated zero
in the current direction until real stop evidence and the calibrated dwell permit
the channel-7 change. The switch frame still has zero throttle; nonzero reversed
magnitude is permitted only afterward. A new session first needs explicit stopped
forward neutral. Unsupported reverse never becomes forward absolute throttle.
Stop evidence cannot be invented from a delay or stale/missing RPM telemetry.

## 5. Avian reverse and telemetry requirements

The [official Avian programming instructions](https://www.spektrumrc.com/on/demandware.static/-/Sites-horizon-master/default/dw8b38e716/Manuals/Spektrum-Avian-SmartLink-Update-Instructions.pdf)
describe **Brake Type = Reverse** and selecting the reverse channel through
Thrust Rev / item 15. Channel 7 is this project's requested assignment; it is
not asserted to be every Avian's fixed/default mapping. The document's separate
recommendation of level 7 concerns **Brake Force**, not proof of reverse channel 7.

Actual SPMXAE1130A Ver. A Brake Type, channel assignment, firmware, threshold/raw
levels, throttle endpoints and stopping/dwell behavior were not read from an
ESC. They remain UNVERIFIED. `reverse_settings_verified` defaults false and the
profile has no assumed Avian endpoints. Tests use explicitly synthetic values.
No configuration write, firmware update or physical reverse command is added.

The native receive service, parser, CRC/context checks, stale withdrawal and
AP_ESC_Telem adapter remain unchanged. They can publish validated voltage,
reported current and FET temperature once actual qualified traffic exists.
Electrical RPM is not mechanical RPM without motor poles. Existing calibrated
RPM/capacity helpers are not configured or fabricated into live telemetry.
The diagnostic frontend slot remains independent of servo/SRXL2 channel numbers.
No actual ESC telemetry was received in this phase.

One-wire unified operation ultimately requires a scheduler that interleaves
qualified commands and telemetry opportunities on that same selected bus and
records successful TX/poll context even though RX is disabled during TX. The
current passive observer is not by itself a complete active motor-controller
service; fresh source timing and producer ownership remain necessary.

**Important existing safety limit:** the prepared native port admits only
14-byte diagnostic handshakes/approved mock poll structures. The new two-channel
control frame is **18 bytes**, larger than the H7's 16-byte FIFO. The current
port rejects it even if the hardware gate were changed. A validated motor backend
would need bounded FIFO refill interrupt/DMA handling, output-safety integration
and verified command/poll scheduling. Do not remove the diagnostic whitelist to
make this simulation appear functional. Physical throttle/reverse remain disabled.

On communication loss the simulation emits no frame; that is not a physical
stop guarantee. A future motor backend must demonstrate its actual stop/timeout
behavior before arming is permitted, without an active PWM fallback.

## 6. Final option comparison and recommendation

| Criterion | A: physical MAIN3, new IO firmware | B: physical AUX7, existing UART4 work |
| --- | --- | --- |
| Keeps current plug | Yes, only if a new serial transport is eventually proven | No; signal lead must move after qualification |
| Native UART data on pin | **No** | **Yes**, PA0/UART4_TX AF8 + HDSEL |
| Existing return-path evidence | IO bdshot capture route, board still unmeasured | FMU bdshot capture route and prepared UART RX, board still unmeasured |
| Additional software | New deterministic timer UART, IO scheduler, IPC/capabilities, telemetry transfer, boot/failsafe/output-ownership redesign | Finish qualified UART session/motor backend, real MAIN3 inhibit/ownership integration, final-demand adapter and command/telemetry scheduling |
| Neighbor impact | MAIN4 shares TIM4; FMU DMA and MAIN1/2 also interact; receiver/system-timer resources tightly allocated | AUX8 retains independent PWM pin use if UART4 never claims PA1/TIM5; IOMCU remains conventional except the future verified MAIN3 inhibit |
| Safety validation | New independent IO safety/stop behavior essential; cannot reuse PWM failsafe semantically | Retain existing vehicle safety and bench gates; still need real motor loss/stop validation |
| Electrical prerequisites | Revision-specific MAIN3 level/buffer/capture/release review, especially possible MAIN 5-V modification | Existing Phase 7 AUX7 release/input qualification still required |
| Extra flight hardware | Not proven necessary, but no firmware-only safe implementation exists yet | Native one-wire plus ground remains intended; compatibility unverified |
| Current decision | NO-GO for native UART; timer/DMA approach UNVERIFIED and not selected | Recommended development path, physical operation still blocked |

MAIN3 is not rejected merely for effort: its absent UART, mixed TIM4 requirements,
limited IO timing/resources and independent failsafe burden do not meet the
requested reliability criteria today. No evidence justifies preferring it over
AUX7. Retain the existing logical function and continue AUX7 qualification. If
keeping the physical MAIN3 plug is mandatory, pause actual motor integration and
treat the timer/IO project as a separate research/proof effort, not a pin remap.

## 7. Actual tests, builds and remaining work

| Validation | Actual result |
| --- | --- |
| New control/routing GTests | 13 PASS |
| Same new standalone cases under ASan/UBSan | 13 PASS; warnings as errors; no reported findings |
| Custom Plane compile/link | PASS, zero compiler warnings |
| Custom Copter compile/link | PASS, zero compiler warnings |
| Linked custom ELFs | RX-only write stub and bench arming veto retained; Control, command/session and physical TX port absent |
| Inherited IO image comparison | Normal and dshot F103 BINs match Git HEAD; no IO image edited or flashed |
| MAIN3 or AUX7 physical bus, motor, reverse or telemetry test | NOT_RUN |

Cases cover the three numbering domains/fixed mask, calibration and reverse
permission, stopped/dwell switching, explicit neutral, disarm/suppression, bad
input/buffers, stale/conflicting ownership, duplicate-output conditions, epoch/
clock wrap and loss/reselection. A generated control frame is also explicitly
rejected by the native diagnostic packet whitelist. Output-inhibit tests are
simulation tests, not physical MAIN3 muting evidence.

| Custom image | Flash used | Free flash | Static Data+BSS |
| --- | ---: | ---: | ---: |
| Plane | 1,591,920 B | 374,152 B | 145,868 B |
| Copter | 1,595,708 B | 370,364 B | 146,960 B |

The new control code is compiled but uninstantiated/dead-stripped from vehicles;
unchanged sizes are not evidence of a functional motor driver. Static RAM is not
peak runtime usage. The existing Docker/WSL2 official ArduPilot image and waf
workflow were used; final `spktsmart-phase8-validation` exited zero. No unchanged
old protocol/resource matrix or standard-board build was repeated.

Logs: workspace `logs/spktsmart-phase8-linux/`. APJ/BIN/ELF and source/artifact
hash manifests: `custom-firmware/Pixhawk6C-SpktSmart-phase8-artifacts/`.
Helpers: `validate-spktsmart-phase8.sh`, `inspect_spktsmart_phase8.py`, and
`collect_spktsmart_artifacts.py --phase 8 --board Pixhawk6C-SpktSmart`.
Board ID 56, decompressed APJ length and BIN payload are checked. Earlier phase
artifacts remain intact. No new flight-readiness or compatibility claim is made.

Required next work, in order:

1. Complete the actual AUX7 electrical evidence in [PHASE7_BENCH.md](PHASE7_BENCH.md).
   MAIN3 would need its own equivalent evidence if that separate project resumes.
2. Validate motorless device discovery and non-motor telemetry polling semantics
   for this exact Avian firmware; retain manual TX authorization gates.
3. Obtain actual ESC settings, endpoints, raw reverse states, motor-pole data and
   verified stop/dwell behavior. No guessed reverse or stop command.
4. Implement a real exclusive-output coordinator with persistent/epoch-safe IO
   inhibit ACK, including reset/failsafe/mixer/GPIO paths, before digital mode.
5. Review and implement the final vehicle-demand hook and bounded motor UART
   backend; test all throttle modes, limits, safety/disarm/RC loss/ESC loss,
   stale telemetry, duplicate producers and recovery. Preserve the bench veto.
6. Only after those reviews and separate explicit hardware authorization perform
   powered motor safety tests. No motor or propeller/EDF rotor is connected for
   initial communication qualification. Never silently fall back to PWM.
