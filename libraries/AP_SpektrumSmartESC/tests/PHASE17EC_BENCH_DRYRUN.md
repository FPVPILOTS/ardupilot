# Phase 17E-C — on-board SRXL2 telemetry poll dry-run (NO TRANSMISSION)

## Existing hardware evidence

- Pixhawk6C-Spektrum AutoLab + Avian 130A, connected via UART4 AUX7.
- Real bench shows correct automatic startup/reconnect: `ID40 TRY2 OK2 LOST1`.
- Linked traffic is `SPK18 FRM H455 C0 T0 O0`: the ESC is sending 0x21 handshake traffic, NOT unsolicited 0x80 telemetry.
- SRXL2 0x80 is returned when the master selects the device in a 0xCD Control Data `ReplyID`.
- 0xCD can contain channel data, and a channel update mask of zero **does not** mean throttle zero / motor stop. Do not send an unproven 0xCD packet to a motor-capable ESC.

Sources: SpektrumRC/SRXL2 open source library; ArduPilot AP_RCProtocol/spm_srxl.h.

## What is implemented on this branch

The existing unarmable AutoLab image performs an **on-board, receive-only poll readiness dry-run** after SRXL2 link establishment. The flightcontroller itself checks:

1. The linked state has remained established for >= 1000 ms.
2. The current peer ID is exactly 0x40.
3. The newest CRC-valid frame from that peer was acquired no more than 100 ms ago.
4. The system is disarmed and emergency stop remains active.
5. UART4 receive mode is restored; line is idle and no transport owner conflict is present.
6. No new UART framing/noise/overrun/error counter appeared after LINKED.
7. A previous competing-master observation latches the preview blocked until link loss.
8. Only one software preview per link; link loss requires a fresh handshake before a new preview.

If conditions pass, software encodes a 14-byte SRXL2 0xCD packet in RAM using a zero channel *update* mask and ReplyID=0x40, validates its CRC and full decoding, then **discards it**. Nothing is sent. Never treat it as motor-safe.

### Important hardware non-transmission guarantee

- No new UART API or hardware TX path.
- `SpektrumUART4::begin_tx()` is unchanged and in AutoLab only permits `AP_SpektrumSmartESC_Reader::handshake_packet()`, which returns false for 0xCD even when its CRC is valid.
- Handshake remains the only physical 0x21 packet type allowed.
- No change to motor, reverse, output, arming policy, telemetry publication or persistent parameters.
- **This branch is NOT capable of retrieving telemetric sensor frames by polling**. Its purpose is to validate prerequisites on the real flightcontroller before any separate 0xCD lab image can be engineered and reviewed.

### Mission Planner diagnostics

Every five seconds:

```
SPK17 SRXL2_LINK_ESTABLISHED
SPK18 FRM H... C0 T0 O0
SPK19 DRY RREADY_PREVIEW P1 B...
SPK19 NO TX; 0xCD PREVIEW ONLY
```

`SPK19 DRY RPREVIEWED P1...` may follow on subsequent samples. Denial reasons include NO_LINK, BENCH_UNSAFE, WRONG_PEER, MASTER_CONFLICT, UART_CHANGED, RX_NOT_READY, PEER_STALE, SETTLING, BUS_NOT_IDLE.

These are predictions, not measurements of yet-unbuilt firmware.

## Local host + firmware build

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
bash libraries/AP_SpektrumSmartESC/tests/build_phase17e_c_dryrun.sh > "$HOME/spkt17e-c-build.log" 2>&1
rc=$?
grep -E 'tests passed|test passed|PHASE17E-C PASS|error:|Build failed' "$HOME/spkt17e-c-build.log" | tail -n 30
echo "Build exit code: $rc"
```

As of Phase 17E-B: 71 previously passing host tests. Phase 17E-C adds 11, so **82 total expected**, not yet executed. Build passes only when actual firmware includes the SPK19 diagnostic strings.

## Prerequisites for next real on-bench polling phase

A **separate** firmware target and independent native UART4 allowlist must be reviewed, after proving that the ESC and motor are physically isolated, signal voltage and ground are safe, and a channel-free SRXL2 Control Data packet can be safely applied. Hardware proof is mandatory; software's `mask=0` is NOT a motor safety interlock. Only then can a single short 0xCD request be tested with strict TX-complete, turn-around, response timeout, one-shot arming blockade and link-loss cancellation.

Do not perform any physical polling using this phase or by modifying its code to bypass the whitelist.
