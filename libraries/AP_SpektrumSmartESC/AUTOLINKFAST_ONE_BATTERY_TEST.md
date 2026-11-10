# 2026-11-10 one-battery Avian / Pixhawk 6C AutoLinkFast test

## Real fault evidence (baseline build 72391813)
When the same battery powered both Avian 130A Ver A FW 04.0.27 and
the Pixhawk 6C, user observed on 2026-11-10 05:56:

- `SPK17 HS TX50 RXB0 CRC0 RP0`, `UART Q134 C134 R134 F0`;
- `SPK24 A0 F0 L0 Q6982 R0`, `SPK24 ERR FE0 NE0 O0 REL0`;
- repeated `HANDSHAKE_TIMEOUT`, `TRY3 OK0 LOST0` while pin HIGH.
- Independent tests showed ESC-only repower (FC stays on over USB)
  makes the 0x40<->0x30 handshake work. Earlier FC-only reboot with ESC
  held on also produced CRC-valid D00 announcements but no targeted
  D30 response and one framing error.

That pattern is consistent with FC application readiness being later
than the ESC's initial SRXL2 handshake/mode discovery window, but
**Avian's specific timing/mode latch is NOT independently established**.
Do not claim PWM fallback is certain merely from RX0.

## Purpose of this variant

`Pixhawk6C-SpktSmart-AutoLinkFast-IOInstall` is a separate
**non-armable**, **0x21-only**, **motor-disconnected** build.

- Original AutoLink: 1000ms policy listen + 2000ms reader prepare after
  AutoLink initialization, before the first autonomous 0x21.
- AutoLinkFast: 50ms policy listen + 50ms reader prepare after AutoLink
  initialization, while preserving half-duplex idle, true UART TC, CRC,
  addressed 0x21 and RX restore gates; timeout 5s after handshake start.
- NO code runs before ChibiOS/ArduPlane boot. It cannot
  guarantee reception at the ESC's initial **battery power-on**.
- Does NOT send 0xCD, command the ESC, drive a motor, override errors,
  or change production motor firmware settings. Special IO firmware is
  still the MAIN3-inhibited image CRC 0x6DBBAD71.
- `SPK25 FAST INIT<uptime_ms> L50 P50` prints the application uptime
  when AutoLink started (not the independently measured ESC power edge).
  `SPK25 INIT... FIRSTTC... LASTTC...` repeats from the 5-second
  status cycle alongside `SPK23` and `SPK24`.

Host CI suite adds
`test_spektrum_autolinkfast.cpp` compiled with
`-DAP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT=1` and checks
50+50ms eligibility, actual CRC-verified target reply, native TC,
RX-restore requirement, no success on an absent ESC and unsafe lockout.

## Build and compare

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
git fetch origin feature/spktsmart-bench6-contract
git merge --ff-only FETCH_HEAD
bash libraries/AP_SpektrumSmartESC/tests/build_autolinkfast_6c_installer.sh \
  > ~/spkt-autolinkfast-build.log 2>&1
rc=$?
echo "AutoLinkFast build exitcode: $rc"
tail -n 32 ~/spkt-autolinkfast-build.log
```

Pass must include: host suite success, exact IOMCU CRC, board ID 56,
`SPK25` strings and `AUTOLINKFAST INSTALLER BUILD PASS`.
If any check fails, DO NOT FLASH.

### Strict conditions for the physical check
1. Three EDF motor phase wires disconnected and individually insulated.
   FC cannot arm; emergency stop stays active.
2. After reviewing APJ / IO hashes and backing up parameters, use
   Mission Planner *Load custom firmware* on this separate target.
   The APJ attempts an IOMCU update.
3. Set/retain `SPKT_ENABLE=1`, `SPKT_AUTO=1`,
   `SPKT_HSID=64`, `SPKT_MOTOR=0`, `SPKT_TLM=0`,
   `SPKT_PROBE=0`, `SPKT_HS=0`.
4. First try the **realistic single battery** power-on,
   no USB cable connected until after flight-controller boot.
   Capture `SPK25`, `SPK23`, `SPK24`, `SPK17` from Mission Planner.
5. Never energize the motor/EDF, disable prearm checks, change a
   power wiring arrangement, or guess an SRXL2 programming command.
   A separate bench PSU or instrumentation for the logic signal is
   acceptable only when wired to avoid BEC backfeed.

### Interpretation
- `SPK25 ... L50 P50` + `RP1` + `LINK_ESTABLISHED` after one-battery
  power-on: the late app-level 3s wait was a contributor. Still NOT
  normal 0xCD/0x80 operation or motor ready.
- `SPK25 ... L50 P50` + `RXB0` after one-battery power-on,
  ESC-only repower works: shortening app-level waits was insufficient.
  Likely next requirement is a **physical early start strategy**:
  compare with official Spektrum receiver boot behavior, measure ESC
  signal startup from battery-on with a logic analyzer, then consider
  qualified FC-first power sequencing or an early SRXL2 bootstrap.
  An arbitrary high-current battery relay or modifying the 130A ESC
  power feed without engineering review is NOT recommended.
- A `D00` announcement with `RP0` is still not a true addressed peer.
  Keep all UART-fault latches as designed.
