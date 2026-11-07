# Phase 17E-D — ONE SHOT physical SRXL2 poll, motor physically disconnected

## DO NOT FLIGHT / DO NOT FLASH BEFORE REVIEW

New **separate** `Pixhawk6C-SpktSmart-PollLab` firmware target. Phase 17D automatic handshake and Phase 17E-C read-only dry-run remain unchanged in the original `Pixhawk6C-SpktSmart-AutoLab` image.

A zero SRXL2 channel update mask is **not** a verified throttle-stop command. The 14-byte `0xCD` command can affect the ESC protocol state. The physical test is permissible only if **all three EDF motor phase leads are fully unplugged**, all actuators are disconnected, and the signal and BEC wiring has been separately checked. The software cannot detect disconnected motor wires. Never rely on disarm alone.

## Exactly what can be transmitted

- Existing 0x21 directed + broadcast handshakes, preserved.
- After **explicit operator parameter challenge**, exactly **one** `0xCD` Control Data frame, 14 bytes, with command 0, ReplyID 0x40, zero channel update mask and no channel data. Byte-for-byte verified at the native UART4 boundary; no variable payload, no motor demand, no reverse.
- The PollLab one-shot native lease expires after 100ms and cannot be rearmed in the same boot, regardless of reset/retry parameter changes.
- No other SRXL2 physical frame types can be emitted. No repeated or periodic polls.
- Native PA0 open-drain half-duplex 115200, bus-idle checks, true-TC interrupt completion and RX restoration are inherited from the working AutoLab.
- Disarmed/emergency-stop, linked ESC ID 0x40, fresh CRC-valid peer handshake, stable dry-run, and no new UART errors are required by high-level gates.
- Link loss, new UART error, failed write or response deadline closes the native lease and increments FAIL.

## Physical test procedure (ONLY AFTER SUCCESSFUL BUILD + INDEPENDENT BENCH REVIEW)

1. Disconnect **all three motor phase wires physically**, as well as actuators, and secure the wiring so they cannot reconnect. Do not power a connected EDF.
2. Verify the ESC is powered from a safe isolated bench supply; do not tie two BEC positive rails together; ensure common GND and a qualified AUX7 3.3V-compatible signal.
3. Load firmware from `build/Pixhawk6C-SpktSmart-PollLab/bin/arduplane.apj`. This variant remains unarmable.
4. Set `SPKT_ENABLE=1`, `SPKT_AUTO=1`, `SPKT_HSID=64`, `SPKT_PROBE=0`, `SPKT_HS=0`, `SPKT_TLM=0` and reboot. Ensure `SPKT_POLLACK=0`, `SPKT_POLLRUN=0` until physically ready.
5. After `SPK17 SRXL2_LINK_ESTABLISHED` and `SPK19 DRY RPREVIEWED P1`, **operator manually** set `SPKT_POLLACK=15` (fresh authorization) then `SPKT_POLLRUN=1` within 10 seconds. Do not save these one-time parameters.
6. Expect `SPK20 POLL TX1 RX1 FAIL0` only when the ESC returns one CRC-valid `0x80` 22-byte telemetry candidate within 12ms of TC; `RX0` means no validated candidate and is not necessarily a physical TX failure. Watch `SPK18 FRM ... T...` and `SPK18 ESC CAND...`.
7. `SPK20 LAST FL... ERPM...` and `SPK20 LAST V... I... T...` show **last unverified candidate values**, not live instrument status. No sample goes into ArduPilot ESC telemetry or flight controls. The frame contains destination but not an independently authenticated source ID.
8. Stop after one experiment and provide logs for review. Do not connect the motor to PollLab firmware.

## Where telemetry appears

First physical test: Mission Planner **Flight Data → Messages**, lines prefixed `SPK20`, plus `SPK18 T` / `ESC CAND`. If no physical `0x80` arrives, no sensor values can be displayed.

Later (not yet implemented): verified source, measured value calibration, contract pole count conversion, index ownership and staleness policy are required before AP_ESC_Telem publishing. The existing `AP_SpektrumSmartESC_Telem` helper reserves zero-based index 14 (ESC 15) for voltage/current/temperature; it deliberately does **not** publish electrical RPM as mechanical RPM.

## Build (NOT previously executed)

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
bash libraries/AP_SpektrumSmartESC/tests/build_phase17e_d_poll_lab.sh > "$HOME/spkt17e-d-build.log" 2>&1
rc=$?
grep -E 'tests passed|test passed|PHASE17E-D BUILD PASS|error:|Build failed' "$HOME/spkt17e-d-build.log" | tail -n 30
echo "Build exit code: $rc"
```

This change adds 6 standalone host tests. The prior host suite had 82 cases, so 88 are expected; not yet locally confirmed. Build script also checks the compiled firmware's PollLab warning and `SPK20` strings.

## Engineering limitations

- The ESC manufacturer has not confirmed the exact no-channel-update 0xCD reaction on this specific Avian ESC revision. This test therefore REQUIRES physical motor disconnection.
- True TC and FIFO restore are software/hardware register measurements; they don't prove signal voltage on the ESC connector. Independent scope checking is recommended.
- Responses within 12ms are only **candidate** sensor packets, not authenticated provenance; a third SRXL2 device/master must not be present.
- An ERROR/FAIL, no telemetry, or any unexpected movement/heat means stop and assess; never override gates to force a response.
