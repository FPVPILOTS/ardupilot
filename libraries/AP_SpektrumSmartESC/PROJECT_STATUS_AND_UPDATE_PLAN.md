# Avian 130A Smart ESC -> Pixhawk 6C / ArduPlane / RP4TD
## Project checkpoint — 2026-11-09 (session interruption recovery)

The official ArduPilot branch in this project is our own fork
`FPVPILOTS/ardupilot` on `feature/spktsmart-bench6-contract`.

### Last physically observed

- Pixhawk6C special IOMCU CRC 0x6DBBAD71 installed.
- Direct AUX7/PA0 SRXL2 half-duplex links: controller 0x30, ESC 0x40,
  115200 baud; valid 0x21 handshakes.
- Motor phases disconnected, no rotation.
- One trial achieved Q9/TC8, and candidate 0x80 telemetry
  reported THpct0 and eRPM0, then age P219 safety abort.
- A later tighter reply-timeout build stopped at Q1/TC1 RX0, so we
  cannot claim a stable three-second trial, armed run, or motor cutoff.
- User-confirmed native ArmedBench-IOMCU firmware **compile** passed
  on 2026-11-09, commit 166e64ae2f, exitcode 0, ArduPlane output
  build/Pixhawk6C-SpktSmart-ArmedBench-IOInstall/bin/arduplane.apj.
  Waf reported 373672 free flash bytes. No native ArmedBench
  firmware **flash** or physical runtime has been verified.

### Software in branch; native ArmedBench now built, not flashed

- Native SRXL2 controller uses FC0 source 0x30 rather than simulation
  0x31; finished 0x21 followed by 0xCD and source-less but solicited
  0x80; no invented periodic 0x21 "heartbeat" requirement.
- Startup handling for ESC-first, FC-first and late RC was revised
  and disarmed-only reconnect attempts added. Not yet hardware proven.
- `Pixhawk6C-SpktSmart-ArmedBench-IOInstall` is a compile-only
  candidate; still unqualified by default. Selecting it **DOES NOT**
  demonstrate safe physical arming or motor TX. The earlier source
  mistake of setting operator/electrical/protocol/endpoint/loss
  qualifications to true merely because of a board macro was repaired.
- The ordinary base board, TurnLab and production mode remain separate.
  No automatic stock-MAIN3 PWM fallback is permitted.

### Official Avian parameter menus

`spektrum_avian_programming_catalog.json` contains vendor names and
published options as a non-operational schema. The app/firmware may
offer different choices on the user's specific Ver A ESC.
`0xCD` subcommand `0x03` exists for SRXL2 forward programming in
the public Spektrum library, but the Avian-specific parameter IDs,
queries, encoding, ACK and write/save protocol are NOT documented by
the consulted public protocol implementation. Do not guess writes.

**As-found ESC programmer screenshots were ALREADY supplied by the user
and discussed 2026-11-07. Do not ask for them again.** Actual settings:
Avian 130A Ver A, firmware 04.0.27; Fixed-Wing; Brake Reverse,
Brake Force 0; Soft Cutoff; Auto Calc cells; Cutoff Voltage Disabled;
BEC 6.0V; Start-up Soft; Timing 15deg; motor CW; Active Freewheeling
Disabled; Governor Gain 1; Auto Restart Time 45s; Restart Accel 1.5s;
Thrust Reverse CH7. Normal aircraft pack 6S, 4S is bench test.
These are copied into spektrum_avian_programming_catalog.json as
user_observed_value (distinct from vendor defaults and SRXL2 readback).
Only ask for NEW wire/prog data or a later changed programmer configuration.

### Suggested milestone acceptance

1. Run host safety and protocol tests after retrieving branch HEAD.
2. Build a **non-arming** production-like native reader (no active
   throttle beyond CH1-minimum) and confirm three boot orders.
3. Confirm source-ID 0x30 query/broadcast and TC-correlated ESC replies.
4. Trace power or UART outage, RC loss, and independent stop on a
   secure setup before promoting any physical motor qualification.
5. Validate CH1 endpoints, ESC rotation behavior and reverse mapping.
6. Only then review and compile a separate motor-enabled bench target;
   never conflate an experimental build with qualified flight firmware.

### Future official ArduPilot releases

Keep the driver under `libraries/AP_SpektrumSmartESC` with narrow,
explicit changes in `ArduPlane` and `AP_HAL_ChibiOS`.
For a new official ArduPilot release or upstream/master:

```bash
cd ~/ardupilot-spkt
git fetch origin feature/spktsmart-bench6-contract
git merge --ff-only FETCH_HEAD
bash Tools/scripts/spektrum_upstream_preview.sh
```

The preview fetches upstream and inventories changes on BOTH sides,
including likely conflicts. It does NOT rebase/merge/reset/flash.
Create a separate worktree or upgrade branch, then migrate patch set,
run host tests, compile, and physical regression test. A stock
upstream ArduPilot firmware upgrade will not contain this private
driver until merged upstream. Long term, propose a generic driver
as reviewable PRs to the ArduPilot project. See:
https://ardupilot.org/dev/docs/git-rebase.html

### 2026-11-09 18:54 host regression checkpoint

The user's WSL hosttest after pulling commit 7d3c3604fc reported
exitcode 250: DedicatedFCMasterAndDisarmedRCBoot failed at the assertion
that throttle had already reached 1000. Root cause is a test ordering
mistake: it crossed an arming edge before the initial disarmed zero
packet was TC-completed and answered. The fixture has been corrected
to TC-complete and accept a zero reply before evaluating ready_to_arm.
This repair has been committed but has NOT yet been rerun on local WSL.
No native ArduPlane ArmedBench image has been built or flashed.

### 2026-11-09 18:54 WSL retest: no first-control TX progress

User retested after test-ordering fix; host tests still exited 250:
`test_spektrum_motor.cpp:356 BusBusyNeverRetriesForever(): fault == ESC_LOSS`
failed. Cause: link supervision checked missing sensor responses only
when `_status.controls != 0`, so a continuously low/busy UART with
**zero completed 0xCD frames** never triggered the new sensor watchdog.

The production motor controller now ALSO enforces a separate
100ms deadline since the last hardware-TC-completed TX after
discovery, independently of whether any control or telemetry packet
has completed. It does not reset on queued TX or bus-idle indication.
Regression tests verify pre-deadline healthy state, post-deadline
fault, and no new TX after fault. Source commit was made but
user WSL host tests have NOT been rerun yet. No firmware flash.

### 2026-11-09 — AutoLink-IOInstall next milestone

Created an EXPLICIT **unarmable 0x21-only** Pixhawk6C installer
`Pixhawk6C-SpktSmart-AutoLink-IOInstall`; it embeds the same
MAIN3-inhibited IO image CRC=0x6DBBAD71. The reproducible build script
is `libraries/AP_SpektrumSmartESC/tests/build_autolink_6c_installer.sh`
and its physical boot-order protocol is in `AUTOLINK_6C_MILESTONE.md`.
The new SPK23 state-change messages capture even sub-second LINKED/
BACKOFF transitions missed by 5-second periodic SPK17 snapshots.
Added and validated host simulations for FC-first late ESC power and
ESC-first no-announcement startup. The GitHub Actions job on
e137e0a3f9 succeeded, including 9 auto-session tests and all 33 motor
mock tests. No AutoLink-IOMCU firmware board build/flash has yet been
reported from the user's WSL machine. The AUTO test cannot transmit
0xCD or arm the motor; stable RUNNING 0xCD/0x80 remains a later gate.

### 2026-11-09 20:15 — AutoLink real hardware result

User supplied Mission Planner log from the real Pixhawk6C
`Pix6C-SPKT-AutoLink` / ArduPlane 4.7.1 at commit `b24dc3a1`.
The observed startup sequence was `WAITING_FOR_ESC` at 20:14:59,
`SPK23 HANDSHAKING` then `SPK23 SRXL2_LINK_ESTABLISHED` at
20:15:00. Verified `SPK17 PEER S40 D30 T21 A1`, `HS TX2 RXB28
CRC2 RP1`, `UART Q2 C2 R2 F0`, and `ID40 TRY1 OK1 LOST0`.
The link stayed established in the visible log through 20:15:24;
read-only observer counted `FRM H481 C0 T0 O0` and `POST RX481`.
Hardware UART showed one existing FE (`E1 FE1`) with no visible
increase and line state L0 at 20:15:04, L1 thereafter. Dry-run
`RPREVIEWED` meant **no real 0xCD TX**. `SPK19 NO TX` applies
to 0xCD motor/poll TX, not the two real 0x21 hardware-TC handshakes.
Expected PreArm `SPKT bench active` continued to block arming.

**Grounded result:** one automatic, source-verified and hardware-
completed dedicated SRXL2 link succeeded and stayed LINKED for at
least 24 seconds. Although WAITING preceded LINKED, the log does
not directly timestamp the ESC supply connection, so do not claim
that all three boot orders or any later power-loss reconnect passed.
**Not yet proven:** ESC-first, simultaneous power-up and unplug/
replug reconnect; sustained normal SRXL2 `0xCD` and solicited
`0x80` traffic, stopped evidence, reverse, or motor arming.
Retain special IOMCU and non-armable profile while evaluating.

### 2026-11-09 20:16–20:17 — simultaneous battery boot FAIL, ESC-only repower PASS

User performed two real **motor-phase-disconnected** tests with
`Pix6C-SPKT-AutoLink` ArduPlane 4.7.1 `b24dc3a1`:

A. Aircraft battery powers both Pixhawk and Avian, then USB/Mission
Planner connection: AutoLink repeatedly sends correct 0x21 handshakes
with true UART TX-completion/RX restoration. At 20:16:17,
`SPK17 HS TX77 RXB0 CRC0 RP0`, `UART Q245 C245 R245 F0`,
`ID00 TRY3 OK0 LOST0`; repeated
`SPK23 WHY HANDSHAKE_TIMEOUT`. Up to five failed attempts and
more than 400 true completed handshake TXs were recorded. Line
L1, no receive frames. This proves FMU sends after boot but the
ESC did not answer; it does **not** prove an exact reason.

B. Keeping Pixhawk on USB and removing/reconnecting ESC battery:
at 20:17:07 `SPK23 LINK SRXL2_LINK_ESTABLISHED`,
`SPK17 PEER S40 D30 T21 A1`, `HS TX2 RXB28 CRC2 RP1`,
`ID40 TRY6 OK1 LOST0`. `FRM H878 C0 T0 O0`
by 20:17:52. UART historical `E5 FE5` appeared around
ESC repower; no observed increase during the stable link.
No 0xCD or 0x80, still the unarmable AutoLab.

**Diagnosis:** boot sequencing is now the primary issue.
`AP_SpektrumSmartESC_Reader::PREPARE_MS=2000` and
`AP_SpektrumSmartESC_AutoConnect::STARTUP_LISTEN_MS=1000`
delay FC's first unsolicited 0x21 query after ArduPilot init,
unless an ESC 0x40->0x00 advertisement arrives to grant early
TX. That is likely far later than an ESC which powers immediately
from the same aircraft pack. Published Spektrum documentation
shows automatic PWM/SRXL2 mode selection in certain Smart Throttle
systems and ~200ms startup requirements for **some receivers**,
but this does **not** establish Avian 130A Ver A firmware 04.0.27
has that exact cutoff. It may instead be another persistent
power-on receive state.

**Discriminator using EXISTING image, no reflashing:** in the
failed simultaneous-power state, leave Avian continuously powered
and command a normal FC-only reboot through Mission Planner (motor
phases still physically unplugged); inspect whether retries ever
produce RXB>0 or RP>0. Then separately reboot *only* Avian while FC
stays powered. If only Avian repower recovers, its latched startup
mode/handshake acceptance timing is favored; if FC-only reboot
recovers, examine FC UART/power state. Avoid any "reset ESC" packet
or guessed firmware programming command. Neither observation
qualifies motor arming.

**Engineering direction:** measure boot-to-first true UART TC and
the Avian power-on edge with an independent analyzer; study whether
early/forced-SRXL2 is available on *this ESC*, not merely on
Spektrum receivers; evaluate MCU early-start handshake before
ArduPlane init, or qualified FC-first supply sequencing. Do not
simply reduce the 2s RX-first gate on a motor-connected shared
PWM/SRXL2 signal without validating false-mode motor behavior.
Do not promise simultaneous boot is fixed by the present code.

### Build status: IMPORTANT

The user supplied WSL `./waf plane` build exitcode 0 for
`Pixhawk6C-SpktSmart-ArmedBench-IOInstall` at commit `166e64ae2f`.
GitHub Actions SRXL2 host suite also succeeded at that commit.
These results establish **build and host tests only**, not
physical SRXL2 cold-start, ESC control, arming, reverse, or failsafe.
The experimental board profile still returns unqualified
(`operator_qualified=false`), and `SPKT_MOTOR` defaults to zero.
No firmware flash was reported.
