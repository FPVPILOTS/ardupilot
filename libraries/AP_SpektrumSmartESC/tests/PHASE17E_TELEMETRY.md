# Phase 17E-A — receive-only SRXL2 ESC telemetry survey

## Verified hardware baseline

- Phase 17D Pixhawk6C AutoLab v9d3b5294 independently established a CRC-valid SRXL2 handshake with Avian 130A at address 0x40.
- After ESC power-off: `WHY ESC_RESPONSE_LOST` / `LOST1`; after power-on: `FAST OK2 NO0`, `TRY2 OK2`, `SRXL2_LINK_ESTABLISHED`.
- During a linked period, `SPK17 POST RX` advanced by ~98 frames / 5 s. Their **packet type had not been logged**. We must not assume those were telemetry packets.

## Protocol and trust model

Official Spektrum SRXL2 packets:
- `0x21` handshake, 14 bytes; source/destination are included.
- `0x80` telemetry sensor packet, 22 bytes, with one destination and one 16-byte sensor payload. It **does not contain a source address**.
- The legacy Spektrum ESC sensor payload begins with ID `0x20`; its RPM is **electrical RPM**, not shaft RPM; scales documented in the existing decoder.
- `0xCD` control frame has a replyID and may carry channel commands; therefore requesting telemetry through it is **not** a passive operation or equivalent to a harmless 0x80 query.
- Master-only reference functionality in Spektrum's open library is not generally published. Never guess physical motor-safe control packet semantics.

Phase 17E-A classifies already-received CRC-valid frames and decodes any inbound candidate `0x80` with destination 0x30 and ESC sensor ID 0x20. It publishes **neither** the candidate nor a guessed source to AP_ESC_Telem. Data expires within 100ms, is invalidated by link loss or a malformed sensor frame, and cannot be used for safety decisions.

## Mission Planner diagnostics

- `SPK18 FRM H... C... T... O...`: 0x21 handshakes, 0xCD control frames, 0x80 sensor frames, other CRC-valid frames.
- `SPK18 ESC CAND... BAD... OTHER...`: CRC-valid, correctly addressed ESC 0x20 sensor payload candidates; rejected sensor envelopes; non-ESC sensor IDs.
- `SPK18 LAST D.. S.. SUB..`: last observed 0x80 destination/sensor/secondary IDs.
- `SPK18 UNVERIFIED FL... ERPM...` and `SPK18 OBS V... I... T...`: displayed **only** when a non-expired candidate is available; explicitly unauthenticated and not published to AP_ESC_Telem.

No new outbound packet type is enabled; native HSLab whitelist remains **0x21 only**. No throttle, reverse, motor TX, or flight arming.

## Build (must pass before flashing)

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
bash libraries/AP_SpektrumSmartESC/tests/build_phase17e_passive.sh > "$HOME/spkt17e-build.log" 2>&1
rc=$?
grep -E 'tests passed|test passed|PHASE17E-A PASS|error:|Build failed' "$HOME/spkt17e-build.log" | tail -n 30
echo "Build exit code: $rc"
```

Host suite now includes seven standalone observer tests covering: CRC verification, frame-type counts, sensor decoding, wrong destination, malformed length, unsupported sensor IDs, missing values, timeout and link loss.

## Next work after receiving real SPK18 logs

- If linked traffic is all `H` and `T0`, receiver has **not** observed unsolicited telemetry. This does not prove telemetry impossible: normal master may need to select a device using a `0xCD` replyID, which is an entirely separate safety review.
- If `T>0`, inspect `D/S/SUB`, candidate count and field mask. Verify sample provenance and real hardware values before enabling downstream publication.
- Before any control-bearing polling, require qualified isolated physical test, disarmed/zero-demand semantics, separate native fail-closed packet whitelist, collision/timeout/overrun policies, and independent review. No flight use.

**Status:** source code added, but the new host tests and cross-compiled firmware are not yet verified. Do not flash until successful build and bench review.
