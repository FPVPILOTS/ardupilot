# Phase 19 — independent MAIN3-inhibited IOMCU image

## Scope

- The target is the *separate* `iomcu-f103-spktsmart` IOMCU firmware,
  **not** the Pixhawk6C/TurnLab FMU firmware.
- Its hwdef includes `../iomcu-f103/hwdef.dat`, removes `PB8
  TIM4_CH3 PWM(3)` and declares PB8 a non-output input pin.
- `AP_IOMCU_SMART_ESC_INHIBIT=1` enables the explicit
  `PAGE_SMART_ESC_INHIBIT` capability and nonce acknowledgment; stock IO
  firmware neither exposes this page nor satisfies the TurnLab ownership
  check.
- All IO-side PWM writes, GPIO configuration/writes, channel enables,
  PWM frequencies and output modes filter channel index 2 / MAIN3.
  No other channel, including physical MAIN7, is intentionally inhibited.
- Default IO images and the FMU's normal 0–100% motor controller are
  unaffected; the 6% request exists only in the one-shot TurnLab script.

## Compile only — no flash

```bash
cd ~/ardupilot-spkt
bash libraries/AP_SpektrumSmartESC/tests/build_phase19_io_companion.sh \
  > ~/phase19-io-build.log 2>&1
rc=$?
grep -Ei 'tests passed|PHASE19|error:|failed|warning:' \
  ~/phase19-io-build.log | tail -n 35
echo "Phase19 exit code: $rc"
```

The script compiles the dedicated IO target into
`build/iomcu-f103-spktsmart/bin/` and prints SHA-256 digests. It **does not
flash**, does not modify `Tools/IO_Firmware`, and does not bypass the
FMU's `AP_IOMCU_FIRMWARE_UPDATE_ENABLED=0` setting.

The build does not itself prove that the user's particular Pixhawk6C has
an STM32F103 IO chip with compatible bootloader/clock/pin mapping.
**Verify actual hardware identification before selecting any IO image.**
Avoid installing or auto-installing either generated image until compatibility
and rollback procedure are reviewed.

## Qualification still required, in order

1. **Host + separate IO image build:** tests and compiled special target;
   check binary size and identify the correct polarity image.
2. **Actual IO chip and electrical pinout:** independently confirm IO
   silicon, PCB revision and MAIN3 PB8 identity.
3. **Physical output isolation:** with motor phases disconnected,
   demonstrate MAIN3 remains inactive over boot, FMU output requests,
   safety, failsafe, output mode/GPIO changes and IO restart. Confirm
   other required outputs, including SERVO7, still work. A self-reported
   nonce/mask acknowledgment is not physical output evidence.
4. **Explicit IO image and CRC:** separately install the compatible
   approved image through an intentional operator action, read the
   actually installed CRC, and observe fresh nonce/uptime acknowledgments.
5. **ESC source profile:** independently qualify CH1 zero/full and CH7
   forward value and motor-stop/link-loss behavior. No placeholder
   constants should mark these as verified.
6. **TurnLab:** authorized one-time 0% → 6% → 0% with independent
   power cutoff and logged TC, RX restoration, peer and outcome.
7. **Production driver:** integrate full-scale 0–100% gas and separately
   qualified reverse without a persistent TurnLab limit; then validate
   live telemetry.

No step above is represented as complete merely by compiling firmware.
