# First real SRXL2 response: disconnected waveform, then one handshake

Date: 2026-11-04. Continues Phase 14C; no GPIO, channel-mapping, motor or broad
protocol requalification was repeated. AI-assisted changes on
`feature/pixhawk6c-spktsmart`, base commit
`dbe792162d06cab66c3475fd5556bf7a120f119e`, with existing local changes preserved.
The source/build manifest identifies the actual uncommitted source.

**Next physical action:** one manually requested waveform from the existing
Phase 14B LAB image, ESC disconnected, using the fixture below. The UART4 driver,
DiagnosticRX driver, fixed-pattern whitelist and LAB hwdef are byte-identical to
their Phase 14B manifest sources. No new waveform firmware is needed.

**Physical result so far:** the user supplied GPIO/static-voltage/RX-high evidence;
there is still no external UART TX trace and no CRC-valid physical ESC response.
Neither handshake transmission nor motor control is authorized by this report.

## 1. Firmware to use

Paths are relative to the workspace root (not the nested ArduPilot repository).

| Purpose | APJ | SHA-256 |
|---|---|---|
| **Use for the disconnected waveform test** | `custom-firmware/Pixhawk6C-SpktSmart-phase14b-lab/arduplane.apj` | `97bde9bbac9f7cd3268abcd335dfd8b34569027cd0e0c5a3b9cd1eb7a8dc75cb` |
| Updated protected Plane reader; **cannot transmit** | `custom-firmware/Pixhawk6C-SpktSmart-first-handshake-artifacts/arduplane.apj` | `bf48cfd7078111d13284e0088c171597f2a55254c053145bdc6415f758a0355a` |
| Updated protected Copter regression; **cannot transmit** | `custom-firmware/Pixhawk6C-SpktSmart-first-handshake-artifacts/arducopter.apj` | `73a5f6ec344820a9db808bc768dee5e2254a8049cc1bc687b971001efa0eca58` |

All are board ID **56**. LAB permits only `55 AA 55 AA`; its handshake gate is
closed. The updated protected images contain the reader improvements below but
both handshake and waveform entry points are compiled return-false stubs.
All motor qualification gates remain zero. No experimental IO firmware is needed
for this test. No image was flashed and no hardware was energized.

## 2. WingFlight comparison and concrete changes

The supplied URL was fetched and pinned to commit
[`da67c91c31f3a87c6e13e6bc8aeb49ce59eef138`](https://github.com/WingFlight/wingflight-firmware/blob/da67c91c31f3a87c6e13e6bc8aeb49ce59eef138/src/main/drivers/srxl2_esc.c).
Exact files and upstream GPL notice are retained under workspace
`sources/phase15-wingflight/`; hashes are in the new manifest. This is an independent
implementation reference, **not a captured Avian 130A exchange or proof of compatibility**.

| Item | WingFlight source | Existing SpktSmart decision |
|---|---|---|
| Identity | `srxl2escDriverInit`, lines 1407-1475: source `0x30`, target initially `0x40`, priority 10, UID `0x4156` | Reader source `0x31`, candidate target `SPKT_HSID=64`, priority 20, device-derived UID. All have valid wire representations; no evidence that our different FC unit/priority prevents a reply. Keep identity unchanged for the first bounded experiment. |
| Startup | Blocking 200-ms boot loop, 45-57-ms retries, alternates destinations `0x40` and `0x00` | Manual request only; wait 50 ms, one addressed query, no retry or destination scan. `0x00` means no destination/slave announcement in Rev K, not universal discovery broadcast. Do not import the retry loop. |
| Reply sequence | `srxl2escProcessHandshake`, lines 588-638: accepts own-ID or destination 0, sends addressed response, then schedules `0xFF` confirmation | Accept only target-to-local reply after our actual TX completion; final `0xFF` baud-selection broadcast. The extra addressed response is not required by Rev K 7.2.1. Do not add it without evidence. |
| Baud | Can change to 400 kbaud immediately from a peer's supported-baud field | Stay at 115200; the final master broadcast selects the rate. Support advertisement alone does not negotiate 400 kbaud. |
| Turnaround | `srxl2escTrySendPendingWriteBuffer`, lines 659-736: buffer-empty or calculated completion +300 us; 80-us quiet period | H743 TC interrupt releases the pin after the final stop bit; 3-ms abort watchdog. Main-task quiet guard is conservatively 2 ms, with 100-Hz diagnostic scheduling. This is not an 80-us response-latency guarantee. |
| Electrical mode | `srxl2escDriverInit`, line 1384: `SERIAL_BIDIR_PP` | Retain open-drain PA0 AF8/HDSEL; push-pull on a different board is not qualification of Pixhawk circuitry. |
| Echo | `srxl2escDataReceive`, lines 774-783: discard a transmitted-length number of RX bytes | RE is off during TX. Never discard the first 14 RX bytes by count: they could be the ESC's entire reply. Own-source/own-UID frames are identified as echoes, never peer responses. |
| Channel packets | `srxl2escQueueChannelDataFrame`, lines 399-481: little-endian mask/values, ascending channels, `0xCD/0`, reply ID for telemetry | Existing encoder matches field ordering and CRC placement. Its CH1/CH7 mask is `0x00000041`; WingFlight's second value is another motor, not evidence for reverse polarity. No channel or telemetry-poll packet enters this experiment. |
| PWM conversion | `srxl2escPulseWidthToChannelValue`, lines 299-307: `(us-988)*64`, saturated | Agrees with the existing native receiver conversion convention. Does not qualify the Avian's actual endpoints, reverse levels, or loss-of-signal behavior. |

The public [Spektrum SRXL2 specification](https://github.com/SpektrumRC/SRXL2)
Rev K sections 6.2 and 7.2 describe receiver/remote masters and dedicated-bus
addressed polling. Neither an FC ID nor the WingFlight variable named
`busMasterDeviceId` proves receiverless Avian master interoperability; that variable
is assigned the replying device's source ID. This remains an explicitly isolated
FC-master experiment. No receiver configuration or native RC SRXL2 singleton changed.

For source comparison only, these constructed query vectors have independent
Python `binascii.crc_hqx(..., 0)` CRCs (not physical captures):

```text
SpktSmart, example UID 0x11223344:
A6 21 0E 31 40 14 00 00 44 33 22 11 C1 B8
WingFlight initial addressed query at the pinned revision:
A6 21 0E 30 40 0A 00 00 56 41 00 00 0D 5B
```

No concrete framing incompatibility was established by this comparison. A real
reader defect was found during the audit: an RX byte acquired just before a manual
request made unsigned elapsed time wrap, prematurely timing out the new attempt.
The reader now excludes/counts those stale bytes before timeout/parsing. New tests
cover normal time and clock wrap.

Additional narrowly scoped changes:

- `AP_SpektrumSmartESC_Reader.h/.cpp`: separate requested peer-response count;
  bounded first-64-byte capture per attempt, including malformed bytes and echoes.
- `AP_SpektrumSmartESC_Diagnostics.h/.cpp`: rate-limited `PEER/ECHO/CRCERR` and UART
  messages; `SPKE` peer/stale counts; `SPKX` raw prefix in hexadecimal, at most four
  16-byte records per attempt. No physical UART bytes exist in SITL's diagnostic
  path; the new integration check verifies no invented `SPKX` records.
- `tests/test_spektrum_reader.cpp`: three focused regression cases.
- Workspace `tools/validate-spktsmart-first-handshake.sh` and
  `tools/verify_spktsmart_first_handshake.py`: incremental validation and manifest audit.

Neither the UART electrical configuration nor a TX permission gate was modified.

## 3. One-time disconnected waveform procedure

Do this once for the actual board/cable/TX configuration. Retain the trace; repeat
only if a relevant driver, electrical mode, hardware path or loading changes.
There is no need to rerun the completed GPIO tests or throttle simulations.

1. Unpower everything. Disconnect the ESC signal, BEC positive and motor wires;
   insulate them individually. Disconnect actuators and servo power. No propeller,
   fan or EDF rotor. Keep MAIN3 physically disconnected from the ESC throughout.
2. Identify the keyed **FMU PWM OUT** connector using the
   [Holybro orientation/pinout](https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports).
   **Pin 8 = AUX7, pin 10 = GND**, pin 9 = AUX8, pin 1 = servo power (not signal bias).
   Pin 1 starts at the controller's right side in Holybro's drawing; a cable-side
   view can be mirrored. Do not count solely by black wire color.
3. Prepare a temporary **3.3-V pull-up fixture**. This LAB driver removes the weak
   GPIO pull-up when entering AF8, so the earlier PROBE=2 voltage is not a bias
   source during UART TX. For the documented 3.3-V board configuration, use a
   **4.7-kohm resistor from a verified controller 3.3-V rail to AUX7**. A documented
   accessible source is the unused DSM RC port **pin 1, VDD_3V3_SPEKTRUM**; verify
   that keyed connector separately. The resistor limits a low-state current to
   about 0.70 mA. This is bench equipment, not a required flight converter.
   Do not use the 6-V BEC, servo rail, 5-V TELEM power, or a logic analyzer's supply
   pin of unknown voltage. Do not modify receiver wiring to obtain this rail.
   If there is no confidently identified 3.3-V source, stop fixture assembly;
   do not substitute a supply by guesswork. Apply no bias to an unpowered Pixhawk.
4. Connect a high-impedance 10x scope probe to AUX7 and its ground to pin 10, with
   a short ground connection. Check scope/USB ground compatibility first. Use
   DC coupling, about 1 V/div, initially 50 us/div and a falling-edge trigger near
   1.5 V. At least 10 MS/s is useful for bit timing; use substantially higher
   sampling/bandwidth to assess edges/overshoot. A 3.3-V-compatible logic analyzer
   can additionally decode **115200, 8N1, non-inverted, LSB-first**. It cannot
   replace the analog voltage/release measurement.
5. Power the Pixhawk from USB. Confirm the fixture stays near 3.3 V and does not
   backfeed another rail. The earlier 3.0-V ESC idle and 3.21-3.30-V GPIO pull-up
   observations remain recorded evidence; this is a fixture sanity check, not a
   new general electrical research phase. Unexpected 5/6-V signal levels are STOP.
6. Save the existing parameter file and current known-good firmware identity.
   Manually load the exact LAB APJ from section 1 through Mission Planner's custom
   firmware workflow, with ESC and actuators still disconnected. Do not install
   the experimental IOMCU image. Do not change SERVO functions or receiver serial
   settings. Record a boot/reboot trace: there must be **no UART burst at startup**.
7. In Full Parameter Tree/List, set:

   ```text
   SPKT_ENABLE = 1
   SPKT_PROBE  = 0
   SPKT_HS     = 0
   SPKT_TLM    = 0
   SPKT_SHADOW = 0
   SPKT_WAVE   = 0
   SPKT_EXP    = 15
   LOG_DISARMED = 1
   ```

   Reboot for ENABLE, then refresh parameters. WAVE and HS must be zero. Confirm
   LAB warning, disarmed state, UART `U1`, idle `L1`, no increasing errors. LAB
   arming inhibition is unconditional; do not bypass it. LOG_DISARMED needs a
   working DataFlash backend/SD card; also save the Mission Planner telemetry log.
8. Arm the **scope trigger**, not the vehicle. Manually write **`SPKT_WAVE=1` once**.
   Expected Messages: `SPKL: one 55 AA 55 AA waveform requested`, then
   `SPKL: TC observed; RX restored`. A denied/aborted request is not a successful
   waveform. The RAM request is consumed, with no boot execution or automatic retry.
9. Save the external trace and decode. Expect exactly four bytes `55 AA 55 AA`,
   approximately **8.681 us per bit**, **347.22 us for 40 contiguous bits**, idle
   high and complete stop bits. There must be no `A6` handshake or `CD` control
   frame. Record analog high/low, rise/fall times, overshoot and any unexpected gaps.
   Aim for clean 0/3.3-V levels and ample margin at each bit's center; a rise/fall
   target under 1 us is a conservative bench margin, not a Spektrum specification.
10. Verify external release, not just TC. A steady high line alone is insufficient.
    One low-current fixture option, reviewed before wiring, is a switchable
    **10-kohm resistor to GND**, initially disconnected. After the burst, with no
    further request, briefly connect that load. With only the 4.7-kohm bias it
    should move toward **2.24 V** (`3.3*10/(10+4.7)`), then return when removed.
    Remaining rigidly high or low means the assumed high-impedance path is not
    established. Existing board pulls/buffers may change this result: investigate
    a mismatch rather than treating the divider calculation as board proof.
    Loading can create UART noise/framing events; separate this interval from the
    unloaded waveform/error assessment. Do not feed a test voltage or waveform.
11. After removing the load, inspect `SPKH`: `CR1` must have UE and RE set (bits
    0 and 2), TE and TCIE clear (bits 3 and 6); `CR3.HDSEL` bit 3 set. `Flg` bit
    3 reports AF8/open-drain, bit 0 UART initialized; `RelErr` must not increase.
    This proves firmware RX restoration, not a received ESC byte. Correlate it
    with the external released line. TC's internal status is not an oscilloscope.
12. Do not send further bursts automatically. Save the evidence table below,
    return WAVE/HS to zero, power down and remove the temporary fixture. The
    initial external TX validation is complete only after the trace is reviewed.

The [STM32 reference manual](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
documents HDSEL and open-drain/pull-up operation at the MCU. The test above checks
the actual connector path; it does not assume undocumented Holybro buffer details
or establish an absolute-maximum input rating from an idle measurement.

| Evidence | Record | Current result |
|---|---|---|
| Board identity/configuration, cable, bias source and resistor | Photo/identifier, supply voltage | Board revision unknown; fixture not tested |
| Exact flashed LAB hash | Section 1 hash/readback identity | File hash verified; flashing not performed here |
| Cold boot/reboot without WAVE request | Scope trace with no burst | NOT MEASURED |
| Four bytes/stop bits, bit period and total duration | Native trace + decoder export | NOT MEASURED |
| High/low, edges, overshoot, line loading | Analog scope trace | NOT MEASURED |
| Released line and firmware RX restoration | Load-step trace + SPKH + Messages | NOT MEASURED |
| Real peer handshake | Bidirectional trace + SPKM/SPKE/SPKX | ZERO VERIFIED RESPONSES |

## 4. Separate handshake-only test, after waveform approval

**Not executable with either APJ currently supplied.** After the external TX
evidence passes, prepare and hash a separate reviewed handshake-only image using
this same transport/reader. Its electrical gate can then be set in reviewed source,
with motor gate still zero, waveform entry disabled, and unconditional bench arming
inhibition. No normal parameter may turn the present image into that qualified
image. Do not enable a compile-time gate based on this report alone.

The existing reader already provides the required bounded exchange:

1. Only after wiring/trace approval, unpower and connect ESC signal to AUX7 pin 8
   and common ground to pin 10. BEC positive remains disconnected/insulated;
   motor leads and all actuators remain disconnected. No MAIN3 connection.
2. Manually install the newly identified handshake-only image with ESC power off.
   Apply the same diagnostics settings, `SPKT_HSID=64`, `SPKT_HS=0`, `SPKT_WAVE=0`,
   and a new EXP label. Verify no startup TX and bench arming/stop latch.
3. Power the ESC separately only after the wiring review. Retain a scope/analyzer
   on the shared signal. Do not connect the bench bias automatically: use the
   approved final bus bias/loading arrangement. This must remain within the
   voltage, rise-time and release limits established for both devices.
4. Manually write **HS=1 once**. At most one addressed 14-byte query and one final
   14-byte baud-selection broadcast can be sent. The latter requires an actual
   valid reply. No throttle/reverse/control/empty-mask polling frame is allowed.
   No retries; reply deadline 50 ms after actual TC, total window at most 200 ms;
   no new TX after 197 ms, allowing the 3-ms TX watchdog. Exit restores RX-only.
5. Inspect messages and the raw trace. First milestone requires a real packet
   `A6 21 0E <target> 31 ... CRC`, in the response window, correct CRC and peer
   metadata. A local `31 -> 40` echo, byte count alone, or `TC observed` is not an
   ESC response. A valid reply identifies a protocol device, not its model/firmware
   solely from the `0x40` address; record the physically attached model separately.
6. After success or timeout, leave HS=0, save logs and power down. No motor commands.
   Silence does not prove electrical failure or an incompatible ESC: device ID,
   FC-master acceptance and startup behavior are still hypotheses for this unit.
   WingFlight's claimed 250-ms ESC listening window is a code comment, not a
   verified Avian specification; do not add boot retries or power-cycle TX scripts.

Telemetry polling in WingFlight uses Control Data. It is deliberately excluded
from this handshake experiment. A separate review is required before such packets
are used, even if channel values appear to represent idle. A successful handshake
does not authorize motor operation or prove MAIN3/IOMCU arbitration on hardware.

## 5. Mission Planner evidence and limits

The existing LAB image reports `SPKL` waveform completion and `SPKT/SPKH` passive
UART status. The new reader reporting below belongs to the updated protected
source and a future electrically qualified handshake image; it was not retrofitted
into the reused LAB APJ.

| Message/log | Runtime meaning |
|---|---|
| `SPKT LINK ... ID.. RX.. TX..` | Reader outcome, accepted peer ID; RX is CRC-valid frames including echoes; TX counts frames only after UART TC |
| `SPKT PEER.. ECHO.. CRCERR..` | Requested valid peer replies, recognized own echoes, invalid CRCs; **PEER=1** is distinct from RX=1 |
| `SPKT UART FE.. ERR..` | Native UART framing and aggregate UART error counters |
| `SPKM` | Actual RX bytes/frames, TX frames/bytes, echoes, timeout/unexpected counts, frame types |
| `SPKE` | Peer reply count and stale buffered bytes excluded from the attempt |
| `SPKX` | First 64 post-request raw bytes, hex, offset and length; includes malformed input/echoes, is not itself a valid-response claim |
| `SPKW` | Gate, completion/ownership flags, TC time, UART faults, reported device priority/baud capabilities/UID |
| `SPKH` | Actual UART registers, line/mux flags, error/release counters and passive silence timeout |

Messages are limited to approximately one group per five seconds; logs one group
per second, with at most four raw-prefix records per attempt. `SPKX.TimeUS` is
**log time**, not per-byte acquisition or wire timing. Use the external capture
for reply timing. If more than 64 bytes arrive, only the prefix is stored;
`SPKM.RXB` retains the byte count. Missing/dropped log records are not evidence of
physical success. A handshake-only link naturally goes silent after the bounded
experiment; it is not a continuously active motor-control link.

## 6. Executed validation and boundary

New logs: workspace `logs/spktsmart-first-handshake/`. New artifacts include
`build-manifest.json`, `SHA256SUMS`, BIN and ELF, in the directory from section 1.

- **13 GTests passed:** 12 reader cases (3 new), plus the actual normal/forced
  arming and ignored-startup-request entry-point test.
- **11 ASan/UBSan reader cases passed.** No broad GPIO/channel suites were rerun.
- Focused disarmed SITL: ignored saved HS, consumed/blocked manual HS, seven
  reader/peer-status records, **zero fabricated raw UART records**, no arming/TX.
- Updated custom Plane: **1,598,140 flash bytes**, **146,432 static RAM bytes**.
  Updated custom Copter: **1,601,896 flash**, **147,524 static RAM**. Both builds
  succeeded with **zero compiler warnings** using the existing Linux Docker/WSL2
  image and Arm GCC 10.2.1. RAM is waf data+BSS, not measured runtime peak usage.
- All 92 manifest source files match the Linux snapshot after LF normalization;
  the other source hashes match Phase 14C. APJ payloads equal BINs regenerated
  from the linked ELFs; board 56 and actual compiled closed TX entry points checked.
- Existing LAB hash rechecked; its six transport/whitelist/hwdef source hashes
  still match Phase 14B. Standard Pixhawk6C and unchanged LAB were not rebuilt.
- Python lint passed. Audit setup initially exposed Windows-vs-LF hashing and
  objcopy gap-fill differences; both audit commands were corrected before the
  final successful checks. No firmware failures were hidden by those corrections.

**GO:** prepare the disconnected, manually triggered LAB waveform measurement
with the reviewed 3.3-V fixture and correct instrument grounding.

**HARDWARE UNVERIFIED / NO-GO:** ESC-connected TX until that external waveform and
release evidence is reviewed. The separate transmitting handshake image is the
next gated build, not an artifact already approved or produced here.

**NO-GO:** throttle, reverse, active telemetry polling and flight. The first real
CRC-valid ESC response still requires the operator's hardware experiment; software
tests have not achieved that milestone.
