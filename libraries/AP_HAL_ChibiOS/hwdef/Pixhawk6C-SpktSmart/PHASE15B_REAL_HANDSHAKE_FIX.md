# Phase 15B — ESC power-on handshake window

2026-11-07. AI-assisted implementation on `feature/pixhawk6c-spktsmart`, base ArduPilot revision `dbe792162d06cab66c3475fd5556bf7a120f119e` plus the manifest's local source hashes. This phase continues the existing HSLab; no parser, vehicle controller or motor-output architecture was replaced.

**Physical result remains unresolved.** The user successfully flashed Phase 15 and observed one MCU-reported TX completion, zero received bytes, zero framing errors, timeout, and RX restoration. This is useful hardware feedback, not proof of an externally valid UART waveform. This development session has not flashed, energized or communicated with an ESC.

## Root-cause assessment

The actual observation is preserved verbatim in workspace `logs/spktsmart-phase15b-linux/user-hardware-observation.txt`. It establishes that the manual gate and timeout path executed. There is **no proven root cause** of the missing reply.

| Candidate | Evidence and priority | Software-verifiable / remaining physical evidence |
|---|---|---|
| A: query too late after ESC startup | First software hypothesis to test. The prior operator-triggered query was necessarily late relative to battery connection. WingFlight explicitly initializes early and cites a 250-ms listening window. | Source confirms that claim and previous timing; it does not establish this Avian revision's exact window. New `HS=2` covers battery connection without a 250-ms operator action. |
| D: external TX/RX path, bias or turnaround | Equally unresolved hardware explanation. `U1/L1/FE0/TX1` cannot establish drive strength, baud waveform, connector reception, rise time or contention. | MCU setup/TC/ownership tested in code; external waveform remains unmeasured. No push-pull conversion or electrical qualification claim was introduced. |
| B/F: identity or sequence differs from an ESC reference | Phase 15 used FC `0x31`, priority 20. WingFlight defaults to FC `0x30`, priority 10 and repeated startup handshakes. | New power-on mode uses the reference FC profile. Old `HS=1` retains `0x31` for comparison. Neither FC identity proves master eligibility on this ESC. |
| C: malformed packet, CRC, baud or receive deadline | Existing construction and new reference vector pass software checks. Both builds select 115200, 8N1. | CRC and bytes checked independently; actual oscillator/peripheral-to-pin timing still requires physical evidence. The 50-ms response window starts at MCU TC, not queueing. |
| E: Avian-specific behavior | Could include different firmware, default target ID, startup mode, electrical drive expectations or initialization not described by generic SRXL2. | No response/capture exists to resolve this. No speculative initialization or Control Data was added. |

Two changes are intentionally combined in `HS=2`: startup-window polling and WingFlight's FC identity/priority. If it succeeds, that alone will not isolate which difference mattered. A successful handshake is not evidence of motor/reverse/telemetry compatibility.

## Reference and license comparison

Pinned WingFlight revision: `2228ab0b91c29b8183b75a4e452ef1424378eeff`, fetched 2026-11-07. The ESC driver is byte-identical to the driver pinned for Phase 15. Newly inspected [init.c](https://github.com/WingFlight/wingflight-firmware/blob/2228ab0b91c29b8183b75a4e452ef1424378eeff/src/main/fc/init.c#L590) places `srxl2escDriverInit()` after serial setup and documents an approximately 250-ms listening window. The [ESC driver](https://github.com/WingFlight/wingflight-firmware/blob/2228ab0b91c29b8183b75a4e452ef1424378eeff/src/main/drivers/srxl2_esc.c#L1412) implements an early 200-ms startup loop.

WingFlight file notices specify GPL-3.0-or-later; its license and unchanged reference files are retained in `sources/phase15b-wingflight/`. [Official Spektrum SRXL2](https://github.com/SpektrumRC/SRXL2) is MIT licensed; the existing applicable notices are preserved. No WingFlight driver code was linked or copied wholesale; this implementation extends the existing isolated ArduPilot reader and native transport.

| Aspect | WingFlight | Phase 15B power-on experiment |
|---|---|---|
| Source / candidate | FC unit 0: `0x30`; target `0x40` | Same FC profile and default target; UID derived from Pixhawk hardware identity |
| Priority / baud field | 10 / initially 0 | 10 / 0, always 115200 |
| Startup | Early boot; roughly 45–57-ms retry intervals in 200-ms loop | Explicit manual request only; 2 s prepared, then a fixed 5 s operator connection window; 50–57 ms from query TC to next eligible query |
| Destination behavior | Alternates `0x40` and `0x00`; answers peer then schedules `0xFF` | Directed `0x40` queries; final `0xFF` only after matching peer reply; recognizes startup announcements to `0x00` without counting them as success |
| Final baud | May switch to 400000 | Final broadcast explicitly selects 115200 |
| Echo / turnaround | Reference has echo-count suppression and software timing paths | No receive-byte-count suppression; checks source/UID for echoes; actual UART TC drives RX restoration |
| Pin drive | Reference selects bidirectional push-pull | Existing PA0 AF8 open drain retained; idle bias and external electrical behavior unqualified |
| Telemetry / motor traffic | Normal driver sends Control Data | Whitelist permits handshake type `0x21` only; no `0xCD`, throttle, reverse or telemetry polling |

The official Rev K specification distinguishes slave announcements with destination `0x00` from master baud-selection broadcasts to `0xFF`. It describes addressed queries on a dedicated UART and a 50-ms idle/recovery procedure, but does not establish the Avian-specific 250-ms limit. We therefore do not relabel `0x00` as a discovery broadcast or imitate a receiver device ID. The reference's 250-ms claim supports testing earlier communication, not declaring interoperability proven.

## Implemented changes and bounds

- `AP_SpektrumSmartESC_Reader.{h,cpp}`: reuse existing CRC/parser; add `PREPARED`, finite power-on query schedule, FC `0x30`/priority 10 profile, announcement handling, correct same-attempt reply validation and final baud-selection handshake. Existing single-attempt mode remains available.
- `AP_SpektrumSmartESC_LabHandshake.h`: extend the native lease with the same deadline, no-TX preparation interval, at least 50 ms between query starts, maximum 100 directed queries plus one final broadcast, and exact source/target/UID validation. One experiment per reboot; a late/stalled task cannot create a catch-up burst.
- `AP_SpektrumSmartESC_Diagnostics.{h,cpp}`: existing `SPKT_HS` gets LAB-only value 2; saved requests are cleared at boot, acknowledgement remains explicit; report precise prepared/window/start/TC/restoration/result states, blocked reason, raw TX bytes and native counters. Existing parameter indices are unchanged.
- `AP_HAL/UARTDriver.h`, `AP_HAL_ChibiOS/DiagnosticRX.{h,cpp}`: explicit mode passed to native authorization; expose quiet RX and queued/TC/restoration counts. In power-on mode a low line may permit **preparation only**, never TX. Errors, unexpected peripheral ownership or invalid configuration still refuse the experiment.
- `AP_HAL_ChibiOS/SpektrumUART4.cpp`: instrument native TX and TC/restoration separately; retain real UART TC interrupt, open-drain drive, exclusive buffer ownership and the 3-ms TX watchdog. No DMA-empty inference or bit-banged serial.
- Existing UART/preflight/frontend tests plus new `test_spektrum_poweron.cpp`: timing sweep, reference bytes, announcements, malformed RX, absent RX, identity rejection, finite retries, motor-frame rejection, TC distinction, abort, reboot request clearing and arming veto.
- Workspace `tools/validate-spktsmart-phase15b.sh`, `verify_spktsmart_phase15b.py`, `test_spktsmart_phase15b_sitl.py`: reproducible build/test/artifact checks continuing the existing Linux environment.

The authoritative throttle function 70 → virtual SRXL2 CH1 and authorized reverse → virtual CH7 were not modified. RC1/SERVO1 remain independent. Ordinary firmware retains compile-time physical-TX lockout; this change does not enable future MAIN3/AUX7 digital motor arbitration. HSLab remains unconditionally unarmable and keeps emergency motor stop latched. It never requests actuator motion or emits motor-bearing SRXL2 frames. It does **not** certify every conventional PWM pin as electrically inactive: all actuators and the ESC's MAIN3 connection must remain disconnected. No experimental IOMCU firmware is installed or required.

### Exact timing

`SPKT_LABACK=15` opens a 10-second request opportunity. `SPKT_HS=2` consumes it and starts:

1. **0–2 seconds:** `PREPARED`; receive/observe only. No handshake TX, even if the line is high.
2. **2–7 seconds:** `TEST_WINDOW_ACTIVE`; a directed handshake may start only if input high, no unread bytes/BUSY/error and all native checks pass. The high state is a bus-idle eligibility check, **not an ESC power-on detector**.
3. Wait 50 ms from actual query TC for its response. If absent, subsequent queries have a 50–57-ms TC-to-query guard with deterministic jitter. The normal 100-Hz task can extend this interval; it never sends catch-up bursts. Frame/absolute-time limits also exist in the native UART lease.
4. On a matching CRC-valid external reply: stop directed queries, wait for RX quiet, send one final `0xFF` handshake selecting 115200, then restore passive RX. Local-source/UID echo and advertisements never satisfy the peer-response test.
5. At the deadline, error or manual abort: expire authorization and restore RX. No new TX starts in the last 3 ms of the lease. Each TX has its separate 3-ms interrupt-driven watchdog. Maximum: 100 directed query frames plus one final; actual quiet-bus traffic is normally lower due to TC time and task cadence. No automatic second window or ID scan.

Before TX the native path checks AF8, open-drain type, HDSEL, no SWAP, UE/RE/FIFO, TE/TCIE state, ownership, input high, UART errors, RX buffer and BUSY. TC means the UART reported transmission complete, not merely FIFO-empty; the ISR releases the pad, clears TE, restores RE/open-drain AF and records the transition. A 1-kHz service enforces native lease expiry. Firmware cannot reliably detect electrical contention while transmitting; these checks do not replace an external waveform measurement.

## Parameters and bench procedure

There is no new persistent auto-start parameter. `HS=2` is a consumed LAB command. Use the Phase 15B APJ below; never flash or reconnect with actuators powered.

| Parameter | Setting |
|---|---:|
| `SPKT_ENABLE` | 1 |
| `SPKT_PROBE` | 0 |
| `SPKT_LABACK` | 0 until manually acknowledging |
| `SPKT_HS` | 0 at setup; 2 only for the prepared experiment |
| `SPKT_HSID` | 64 (`0x40`) |
| `SPKT_WAVE`, `SPKT_SHADOW`, `SPKT_TLM` | 0 |
| `SPKT_EXP` | 151, a log label only |
| `LOG_DISARMED` | 1 |

Keep standard arming/safety settings intact. Do not configure UART4 through `SERIALx_PROTOCOL/OPTIONS`, change aircraft throttle/RC mappings, or disable arming checks.

1. Disconnect the ESC battery. Disconnect and individually insulate all motor leads and the BEC positive/red wire. Remove propeller, EDF rotor or fan. Disconnect all other actuators and the ESC's MAIN3 signal connection. Only common ground and the eventual AUX7 signal are involved. The 6-V BEC setting is **not** a signal-voltage specification.
2. With ESC signal disconnected, back up parameters and the old Phase 15 APJ. Manually flash `arduplane-phase15b-hslab.apj` over USB; no automatic flash or TX is performed by this work. Set the setup parameters above and reboot. Verify `SPK15B HSLAB`, disarmed state, consumed `HS=0/LABACK=0`, zero queued/TC counts and UART RX readiness. LAB arming protection cannot be cleared by ordinary parameters.
3. Remove power before wiring. After acknowledging the still-unmeasured TX electrical behavior, connect ESC signal to FMU PWM OUT pin **8 / AUX7**, and common ground to pin **10 / GND**, using the [Holybro controller-side connector orientation](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports). BEC positive remains insulated. No receiver, converter or second output shares the signal.
4. Power Pixhawk by USB **with ESC battery still disconnected**. Verify LAB identity, RX initialization and zero UART faults. A low input is not interpreted as powered-off; it only prevents actual TX. If preparation reports `BLOCKED_UART_ERROR`, `BLOCKED_UART_PREFLIGHT`, or another blocked state, stop and save logs. Do not clear errors or defeat a check to force the experiment. An unpowered/floating line may itself make preparation impossible; this is a reported limitation, not evidence that the ESC was detected.
5. Have the ESC battery connector ready. Write **`SPKT_LABACK=15`**, then within ten seconds write **`SPKT_HS=2`**. Expect `PREPARED`, then after two seconds `TEST_WINDOW_ACTIVE`. Connect the ESC battery promptly during the **first three seconds of that five-second active window**; leave time for ESC initialization and response. You do not need to operate Mission Planner within 250 ms of connection. The firmware polls through the remaining bounded window rather than guessing an edge from line voltage.
6. Keep motor leads disconnected throughout. After a response or expiry, wait up to five seconds for the rate-limited summary. Save the Mission Planner `.tlog` and DataFlash `.BIN`, including `SPKY`, `SPKU`, `SPKM`, `SPKE`, `SPKX`, `SPKW`, `SPKH`, firmware SHA and experiment number.
7. Abort with `SPKT_HS=-1` or `SPKT_ENABLE=0`. For immediate physical shutdown, disconnect the **ESC battery**; a Mission Planner parameter cannot guarantee instant delivery. Authorization expires automatically and cannot be renewed without reboot. Do not repeatedly power-cycle or start further experiments before reviewing the result.
8. Remove the ESC battery first, save logs while USB remains connected, then remove USB before rewiring. No motor, reverse or telemetry-polling test follows automatically.

Old `SPKT_HS=1` remains an explicitly authorized 200-ms comparison test with FC `0x31`/priority 20; it is not the startup-window test. Either accepted mode consumes the single experiment for that boot. Saved values of 1 or 2 never trigger at reboot.

## Messages and interpretation

| Message / log | Exact meaning |
|---|---|
| `SPK15B HSLAB U1 RX1 ACK0 USED0` | New build identity; UART ready; MCU RX state; pending acknowledgement; consumed attempt |
| `SPK15 PREPARED` | Authorization consumed; two-second observation period, no TX |
| `SPK15 TEST_WINDOW_ACTIVE` | Five-second bounded polling window reached; not evidence of battery detection |
| `SPK15 TX_STARTED; UART queued` | First allowed frame submitted to the UART transaction; not proof of external waveform |
| `SPK15 TX_COMPLETE; UART TC` | First actual UART TC observed by firmware; not FIFO/DMA-empty |
| `SPK15 RX_RESTORED; MCU state` | First TC followed by checked RE/pin configuration; not measured connector high impedance |
| `SPK15 QUEUEDn TCn RXRESTn` | Native accepted transactions, successful TC events and TX-to-RX restorations; abort restoration can increment RXREST without TC |
| `SPK15 RX_BYTES=n ESC_ID=xx` | Actual acquired byte count and accepted peer ID in hex; malformed bytes count too |
| `SPK15 HANDSHAKE_OK` | Matching CRC-valid peer reply followed by final handshake TC and checked RX restoration |
| `SPK15 TIMEOUT_NO_RX` | At least one completed handshake TX, no acquired RX bytes before the deadline |
| `SPK15 TIMEOUT_NO_PEER` | RX existed but no completed accepted exchange; inspect CRC, echo, raw bytes and peer count |
| `SPK15 TIMEOUT_FINALIZE` | A matching peer replied, but the final baud-selection handshake did not complete before the deadline; preserve that partial physical evidence |
| `SPK15 TIMEOUT_NO_TX` | No completed TX before deadline; check native QUEUED/TC and idle eligibility. Partial TX may have occurred if a transaction faulted |
| `SPK15 ERROR` | Unexpected traffic, aborted request, native failure or incomplete RX restoration; inspect logs, do not retry blindly |
| `BLOCKED_ACK/USED/REQUEST/BENCH_STATE/UID/UART_UNAVAILABLE/UART_NOT_READY/UART_ERROR/LINE_LOW/UART_PREFLIGHT` | Specific failed prerequisite; no authorized transmission from that request |
| `SPKY` | Each accepted outgoing frame: type, destination, CRC check, exact packet hex; queueing log, not wire capture |
| `SPKU` | Native queued/TC/restoration counts and CR1/CR3/ISR sampled in the logger |
| `SPKM/SPKE/SPKX/SPKW/SPKH` | Existing session counters, peer/echo/stale status, first 64 acquired RX bytes, peer identity/baud and UART errors |

Event messages are emitted once per experiment/transition; counters are rate-limited to five seconds. Repeated query frames are recorded in bounded DataFlash traffic rather than flooding STATUSTEXT. `CRCOK` includes CRC-valid echoes; success requires the expected **external source** (`0x40` by default), destination `0x30` for mode 2, valid CRC and response timing. `ID00`, `PEER0`, a local echo or an advertisement is never a discovered ESC. The build does not synthesize a peer response.

`TX_COMPLETE` with `RX_BYTES=0` narrows the observation to a functioning MCU transaction path with no observed reply. It does not establish correct external amplitude, rise/fall time, inversion, baud, line release or receiver signal path; neither does it prove the ESC is incompatible. Review timing/profile/wiring and retain the raw evidence. No telemetry measurements are claimed or fabricated. Control Data polling remains forbidden even if needed to obtain telemetry.

## Build and test evidence

**IMPLEMENTED / TESTED / BUILD VERIFIED**, 2026-11-07. Flashable artifact:

```text
custom-firmware/Pixhawk6C-SpktSmart-phase15b-hslab/arduplane-phase15b-hslab.apj
SHA-256: 5fa883bebaebd78d90d16c463bcb86de16c6ddded28eb3e43458636dd9a5c111
```

`arduplane.apj` in that same directory is an identical alias. The `.bin`, `.elf`, source/build manifest, checksum file and this procedure are alongside it. Board ID is **56**, target `Pixhawk6C-SpktSmart-HSLab`, runtime identity `SPK15B HSLAB`. APJ image size is 1,602,528 bytes including padding. Protected normal regression images are in `custom-firmware/Pixhawk6C-SpktSmart-phase15b-regression/`; those do not permit physical handshake TX.

| Build | Result | Waf flash bytes | Static data+BSS | Compiler warnings | APJ SHA-256 |
|---|---|---:|---:|---:|---|
| HSLab Plane | PASS | 1,602,520 | 146,492 | 0 | `5fa883bebaebd78d90d16c463bcb86de16c6ddded28eb3e43458636dd9a5c111` |
| Protected normal Plane | PASS | 1,598,752 | 146,444 | 0 | `8fdcf0e9e709eb3cac5be41bcd013da3bbf8a0bc69e692b38120c6c21f84887c` |
| Protected normal Copter | PASS | 1,602,508 | 147,536 | 0 | `1299cc4c58ee56bf59f64ae1ed84d494b40f85622bc5dac7983f8b7cd77146e9` |

Static RAM excludes dynamic stacks/heap. All three images bundle the existing stock IO image, SHA-256 `38c89aec93f3acd69c9f6065347e2090615e4f1e87bc72eefdd3dca347188f1e`. No new IO image was built, uploaded or selected.

| Validation actually run | Result |
|---|---|
| GTests: HSLab authorization 6, existing reader 12, new power-on sequence 7, frontend integration 5, actual arming entry points 1 | **31 passed** |
| ASan/UBSan: authorization 6, standalone reader 11, power-on sequence 7, ordinary native UART methods 9, LAB native UART methods 8, preflight 3 | **44 passed**, no sanitizer errors |
| Power-on timing sweep | Simulated battery connection at 128 offsets across the useful window; 100-Hz task cadence; peer listening from simulated boot+50 to boot+250 ms; all expected exchanges completed |
| Native register-method tests | Reject early TX and tight retries; distinguish FIFO-ready from TC; retain RE/OD restoration; enforce native 7-second/frame bounds; reject motor frames; preserve ordinary lockout |
| Actual Plane SITL, no UART/ESC | Saved `HS=2/LABACK` ignored; forced arm denied with arming checks disabled; absent UART blocks authorization/TX; manual abort consumed |
| Independent packet check | Python `binascii.crc_hqx` gives `0x0D5B` for `A6 21 0E 30 40 0A 00 00 56 41 00 00`; generated frame matches byte-for-byte in GTest/ASan. UID `0x4156` is a fixture, not the real board UID |
| Compiled binary audit | APJ/BIN/ELF payloads match; board ID and physical gates checked; ordinary TX entry points are false-return stubs; LAB waveform method remains disabled; no active Motor/ChannelData/IOOwner backend linked |
| Source continuity / Python lint | 96 project source hashes match delivered manifest; 15 changed/new C++/test files; Phase 14C mapping and hardware definition unchanged; new Python scripts pass flake8 |

The GTest reader includes one additional native SRXL2-reference check not in the standalone sanitizer executable, hence 12 versus 11. Simulated/register-model tests are not electrical measurements or actual Avian replies. Previously completed GPIO and vehicle channel-mapping suites were not repeated.

Build environment remains `ardupilot/ardupilot-dev-chibios:v0.1.3`, image digest `sha256:b5e26d870789f892c50024422d1ed161be3e5a7514d5b3e679415f025493f4db`, Arm GCC 10.2.1. Reproduce with workspace `tools/validate-spktsmart-phase15b.sh` and the existing isolated `/build/src` volume. It configures `Pixhawk6C-SpktSmart-HSLab` at `/build/phase15b-hslab` then runs `./waf plane -j6`; normal Plane/Copter are rebuilt separately. The container has no physical devices and networking is disabled during validation.

Complete build/test logs, SITL output and `verification-summary.json` are in workspace `logs/spktsmart-phase15b-linux/`. The manifest records the existing base Git revision plus local hashes; no fictitious new commit is claimed. A non-fatal copied-submodule Git metadata diagnostic remains in the build logs; the APJ Git identity and current project hashes were separately verified. Existing Phase 15 APJ and its original SHA were rechecked and remain unchanged for rollback.

## Rollback and decision

Before rollback disconnect ESC battery, signal and actuators. Manually load the preserved Phase 15 HSLab `custom-firmware/Pixhawk6C-SpktSmart-phase15-hslab/arduplane.apj`, SHA-256 `7ab1c3f6355c08fbd5003cbc98f702a7a134c7b521ada37211baf92b7a410778`, restore safe setup values (`HS=0`, `LABACK=0`, `PROBE=0`, `WAVE=0`) and reboot. Confirm its old identity. This remains LAB firmware, not flight firmware. To return to conventional aircraft use, restore the separately saved ordinary firmware/parameters with actuators disconnected and complete the usual ground checks. No experimental IO upload is part of installation or rollback.

**Electrical suitability: UNVERIFIED. Hardware handshake: NOT YET ACHIEVED. Motor/reverse/flight: NO-GO.** Software readiness authorizes only the user's explicitly acknowledged, supervised handshake-only experiment. External electrical uncertainty is documented rather than represented as passed or used to prevent the software work. The next milestone is an actual CRC-valid Avian reply with its device ID, not another simulator result.
