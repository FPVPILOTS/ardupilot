# Pixhawk6C / Avian 130A — AutoLink boot and reconnect acceptance
## Scope
A **dedicated non-arming AutoLink Lab** image checks boot-order-agnostic,
automatic SRXL2 0x21 handshake on AUX7/PA0 at 115200 baud. It embeds
the same special MAIN3-inhibited IOMCU. This image CANNOT send 0xCD,
throttle, reverse or Smart ESC configuration writes and is NOT suitable
for flight. A 0x21 handshake alone does NOT prove the ESC will respond
to normal channel commands or stop when signal is lost.

### Source
Board: `Pixhawk6C-SpktSmart-AutoLink-IOInstall`.
Build: `bash libraries/AP_SpektrumSmartESC/tests/build_autolink_6c_installer.sh`.
Expected build output: `AUTOLINK INSTALLER BUILD PASS` and Pixhawk6C
board ID 56, with special IO CRC `0x6DBBAD71`. Verify `Build exitcode: 0`
before considering flashing. **Flashing .apj attempts an IOMCU update.**

### Preflight
- REMOVE and separately insulate all THREE motor-phase wires. Secure
  the 90mm EDF; do not run motor or arm any servos in this test.
- Use a controlled ESC supply / external cutoff; avoid unsafe
  power-backfeeding via BEC or serial signal. Review common ground,
  signal-level compatibility, and independent FC power.
- In Mission Planner: SPKT_MOTOR=0, SPKT_ENABLE=1,
  SPKT_AUTO=1, SPKT_HSID=64, SPKT_PROBE=0, SPKT_HS=0,
  SPKT_TLM=0, SPKT_TURNRUN=0 if present.
- Set those persistent parameters once, then reboot the Pixhawk.
  Emergency stop MUST remain active; the image is unarmable by design.
  If a required setting is missing, STOP and record it; don't guess.
- Clear the Messages window only after noting the firmware banner and
  any pre-arm errors. Capture timestamps for SPK17 and SPK23.
- `SPKT_AUTO=1` creates automatic handshake sessions on boot. Do not
  manually set SPKT_HS or LABACK; this is the AUTO path.

### Hardware acceptance matrix (motor phases DISCONNECTED)
1. **ESC powered first**: connect ESC supply, then boot Pixhawk.
   Expect SPK23 WAITING_FOR_ESC -> HANDSHAKING ->
   SRXL2_LINK_ESTABLISHED, SPK17 ID40 TRY>=1 OK>=1.
2. **Pixhawk powered first**: boot Pixhawk with ESC supply OFF.
   Later connect ESC. Expect a bounded attempt/backoff/retry and a
   valid handshake when the ESC becomes available; attempts must grow,
   successful must grow, bad peer must NEVER be treated as success.
3. **Both powered together**: reapply supply; expect an eventual
   CRC-valid directed handshake and broadcast confirmation.
4. **ESC power removal / re-add (disarmed)**: while observing a link,
   power OFF the ESC, leave FC ON, then power ON ESC. Expect
   ESC_RESPONSE_LOST / RECONNECT_DELAY / new handshake.
5. **Safety gates**: arming remains blocked, no CH1/reverse control
   frames, no MAIN3 PWM fallback, and no UART or IO error count
   unexpectedly rising.

### How to interpret SPK23/SPK17
- `SPK23 LINK ...` is emitted on *every link-state transition*.
- `SPK23 WHY ... TRYn OKn LOSTn` gives reason and cumulative counters.
- `SPK17 ...` still provides a periodic 5-second snapshot and native
  UART queue/TC/RX-restore counters.
- A short `SRXL2_LINK_ESTABLISHED` followed by `ESC_RESPONSE_LOST`
  can occur because this LAB sends 0x21 ONLY. After handshake, an ESC
  may stop its spontaneous 0x21 announcements until a normal 0xCD
  master starts sending control. Do not call that a proven RUNNING
  motor/telemetry link. The next milestone tests the **normal** 0xCD
  scheduling and solicited 0x80 replies on a separate qualified setup.

### Pass criteria
Capture at least one CRC-validated, directed 0x40->0x30 handshake
and a completed FC broadcast with native UART TC and RX restoration
for each of the three power orders. Confirm retries when absent,
and no arming, motor or reverse traffic. A persistent stable
motor-control link is explicitly **NOT** claimed by this lab.
If FAIL or any UART fault, stop further electrical tests and
send the full SPK17/SPK23 lines rather than relaxing watchdogs.

### Build command
```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
git fetch origin feature/spktsmart-bench6-contract
git merge --ff-only FETCH_HEAD
bash libraries/AP_SpektrumSmartESC/tests/build_autolink_6c_installer.sh \
  > ~/spkt-autolink-build.log 2>&1
echo "AutoLink build exitcode: $?"
tail -n 35 ~/spkt-autolink-build.log
```
