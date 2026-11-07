# Phase 9 pre-flash audit: BENCH-FLASH GO

2026-11-02, Europe/Amsterdam. AI-assisted, read-only audit of the saved Plane
artifact. GO applies only to the disconnected, disarmed USB bench procedure below.
No physical hardware was inspected, flashed or electrically qualified. NOT a GO
for connecting an ESC, exercising motors, bidirectional communication or flight.

## Exact artifact

Path from workspace root:
custom-firmware/Pixhawk6C-SpktSmart-phase9-artifacts/arduplane.apj

SHA-256:
feadfd873f640370ec5d3c1c7174458ffb4f3195b14419d471802014bfcda573

APJ size 1452833 bytes; decoded firmware 1592472 bytes; board ID 56.
Source base dbe792162d06cab66c3475fd5556bf7a120f119e with local project changes;
this is not claimed to be a clean commit or an upstream release.

## Nine-condition decision

| Check | Result and boundary |
| --- | --- |
| 1. Artifact/source/manifest | PASS: all 48 project source hashes match current files and LF-normalized Linux snapshot; both saved manifests agree; artifact hashes match. Saved ELF equals the linked build ELF and objcopy with the normal 0xff gap fill regenerates exact BIN. APJ decompresses to that BIN. Manifest is project-scoped, not a signed attestation of the entire repository/toolchain. |
| 2. Board ID | PASS: APJ=56; Pixhawk6C application and bootloader use TARGET_HW_PX4_FMU_V6C, mapped to 56 in Tools/AP_Bootloader/board_types.txt. Operator must confirm actual board is Pixhawk 6C and allow uploader board checking; never force a mismatched board. |
| 3. Smart transmission impossible through parameters | PASS within this firmware's supported parameter interfaces: physical port/handshake/motor engines are absent from the linked Plane ELF. RX _write returns zero. Compiled native port acquire returns false with no register writes/calls, gate=0. RX initialization uses RE/UE/FIFO with TE clear, no TDR transmit or TX DMA/IRQ path. No SPKT parameter changes that compile-time gate. This is about Smart motor/handshake commands, NOT all conventional motor outputs, arbitrary externally supplied code or replacement firmware. |
| 4. Shadow read-only | PASS: default 0; exactly 1 calls only the post-output observer/logging path. No PWM write, mute, transport or model qualification. Existing disarmed SITL produced 11 SPKS records. Shadow is not an arming veto or a motor-stop function. |
| 5. MAIN3 | PASS for shadow integration: no IO image, IO protocol, logical throttle function or normal PWM production is changed by SHADOW. Existing RX bench mode intentionally latches E-stop/arming protection, which can suppress MAIN3; that documented behavior remains. No claim that MAIN3 is always electrically inactive. |
| 6. AUX7 ownership | PASS for this passive build: generated HAL_PWM_GROUPS and HAL_GPIO_PINS contain no PA0 entry. UART4 is not assigned to a user serial port. DiagnosticRX compile guards exclude other UART4 drivers. Only the dedicated RX path can acquire PA0; pin starts input, RX uses open-drain AF8 with TE clear. SERVO function/protocol parameters cannot restore the removed PWM7 pin. This is not the still-missing future digital-mode MAIN3/AUX7 arbiter. |
| 7. Phase 6 protection | PASS: unchanged receiver/bench/arming sources match manifest. Existing real arming entry-point tests and 8 receive tests passed in Phase 9. Native ELF retains bench veto and RX write stub. Nonzero ENABLE latches veto; disabling requires reboot to clear. |
| 8. First test | PASS as a procedure: unarmed, USB only, ESC/actuators and other external wiring disconnected. No unknown external voltage reaches AUX7. Actual physical disconnection and board identity must be checked by the operator before powering it. |
| 9. Migration/rollback | PASS: procedure below. Actual operator backups and current installed firmware version have not been provided or verified. |

Evidence: logs/spktsmart-phase9-preflash/host-integrity.json, native-audit.log,
HAL_PWM_GROUPS.txt, HAL_GPIO_PINS.txt, native-port-gate.txt and RX disassembly.
Reproduction: tools/audit_spktsmart_phase9_preflash.py in the established Linux
image with the snapshot/workspace mounted read-only. No firmware recompiled.
The initial audit used objcopy's default zero-fill, unlike the build's erased
flash 0xff convention; correcting the audit setting yielded exact payload equality.
A generated-header assertion was also adjusted to the actual convention: UART4
serial entries are absent, rather than explicitly defined FALSE in hwdef.h.
No firmware defect or source/artifact change was needed.

## Before a manual flash

1. Keep flight battery, ESC power, servo rail power and all actuator connections
   absent. Remove all MAIN/FMU PWM plugs, including AUX7; disconnect external
   peripherals that could inject power/signals. Do not connect an ESC/test source.
2. On the previously installed firmware, record the exact vehicle/version/board
   and save a full parameter file using Mission Planner's CONFIG / Full Parameter
   Tree, Save to File. Keep a dated untouched copy and relevant logs/missions.
3. Obtain the exact previously working Plane APJ for Pixhawk6C, record its hash
   and retain it offline. Prefer the firmware actually previously used; do not
   assume the custom Phase 9 binary is a rollback image. If no backup/rollback
   artifact is available, do not proceed with a discretionary flash.
4. Check this APJ SHA-256 again using Get-FileHash -Algorithm SHA256. Confirm
   Holybro Pixhawk 6C identity/bootloader compatibility; do not change bootloader,
   BIOS, DFU settings or bypass the uploader's board-ID mismatch check.
5. If choosing to proceed manually, use Mission Planner Load custom firmware and
   select ONLY the APJ above. This audit does not issue or execute that action.
   Ordinary ArduPilot startup may synchronize its bundled IOMCU firmware if the
   installed IO image differs. Phase 9 uses unchanged standard IO blobs; it does
   not guarantee that an older installed IO will never be updated on first boot.

## Parameters and first USB-only run

Normal firmware loading commonly preserves parameters; do not rely on preservation
as your only backup. Let the loaded version perform its normal parameter handling,
then read back and compare with the saved original. Do not bulk-load a parameter
file from another vehicle/version without reviewing conversions, obsolete names,
new defaults and servo/RC/safety assignments. No parameter IDs were renumbered by
Phase 9: existing SPKT entries 1..3 remain and SHADOW is the new entry 4.

1. Reconnect USB only, with all outputs/external wiring still disconnected. Keep
   the board unarmed and the safety switch engaged. Inspect startup messages and
   firmware/board identity. Confirm SERVO3_FUNCTION=70; no output function needs
   changing for shadow observation. Do not lower ARMING_CHECK/ARMING_REQUIRE to
   make the bench test pass; do not arm or invoke servo/motor tests.
2. Verify SPKT_TLM=0, SPKT_SHADOW=0 and the actual saved SPKT_ENABLE value. Defaults
   do not overwrite old saved parameters. For a protected RX+shadow bench session,
   set SPKT_ENABLE=1, SPKT_TLM=0, SPKT_SHADOW=1, then reboot on USB only. The
   disconnected RX pin is not driven; a missing ESC is expected. This combination
   deliberately latches the Phase 6 arming veto/E-stop. Do NOT clear it to arm.
3. With a suitable microSD card, temporarily enable LOG_DISARMED=1 and confirm a
   file logging backend, recording previous settings first. After reboot, confirm
   the RX-only/arming-inhibited notice and DISARMED status. Inspect SPKS log records:
   ~1 Hz, valid only when Flags bit 0 is set; Flags bit 1 must remain clear. PWM and
   percent are commanded/cache values, not measured pins. No traffic is expected
   with AUX7 disconnected. This is observation, not a motor-response test.
4. Stop on unexpected arming, reset loops, changing board identity, parameter loss,
   unavailable protection, or any observation contradicting this procedure. Unplug
   USB; retain logs. Do not connect ESC/actuators to investigate.
5. To finish, set SPKT_SHADOW=0, SPKT_TLM=0, SPKT_ENABLE=0, restore temporary logging
   settings and reboot. Read back the values. Clearing ENABLE alone does not clear
   the arming latch until reboot. Keep all actuators disconnected throughout.

This protected session intentionally changes bench suppression; it is not a test
of active MAIN3 throttle behavior. Actual AUX7 electrical suitability remains
UNVERIFIED. Existing Phase 7 electrical gates still apply before any ESC or test
signal is connected, even for passive reception.

## Rollback

Disconnect all external power/outputs again. Manually install the retained known
working Plane Pixhawk6C firmware with the normal loader and board checks. Reconnect
USB only, confirm version and inspect parameter conversion messages. Compare
against the PRE-UPGRADE backup; restore only compatible settings for that exact
old version. Do not restore a Phase 9-exported parameter file wholesale into an
older firmware. Unknown SPKT parameters may be absent in stock firmware.

If a factory reset is necessary, do it only after the backup is secured and follow
the release documentation; revalidate required calibration, output assignments,
RC mapping, arming/failsafes and safety configuration before reconnecting actuators.
A rollback is not proof of flight readiness. No automatic flash, IO update command,
parameter reset or restoration was performed by this audit.

Official procedural references:
- https://ardupilot.org/planner/docs/common-loading-firmware.html
- https://ardupilot.org/planner/docs/common-loading-firmware-onto-pixhawk.html
- https://ardupilot.org/planner/docs/mission-planner-configuration-and-tuning.html
- https://docs.holybro.com/autopilot/pixhawk-6c/pixhawk-6c-ports
