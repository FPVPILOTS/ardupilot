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
- The last user-confirmed build pass was TurnLab; no subsequent
  native ArmedBench build/flash/test has been verified in this chat.

### Software currently in branch but NOT yet user-built

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

### Build status: IMPORTANT

No connected local WSL executor exists in this chat. GitHub writes
are not the same as a local compile or measured ESC behavior.
Do NOT claim the new ArmedBench image passed the host tests or
the physical trial without user-provided build output and logs.
