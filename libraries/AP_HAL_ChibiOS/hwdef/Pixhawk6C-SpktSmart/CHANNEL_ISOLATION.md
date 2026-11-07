# Phase 8 channel isolation verification

Date: 2026-11-02. AI-assisted test changes; no physical measurements.

## Exact numbering and integration boundary

| Domain | Meaning | Current implementation |
| --- | --- | --- |
| SERVO1 | User aileron output | Not an input to Control |
| SERVO3_FUNCTION=70 | User-assigned logical throttle output | Future adapter must supply final controller demand; no live hook exists |
| RC input 1 | Receiver input, potentially processed by vehicle controls | No direct input to Control; legitimate controller response is not prohibited |
| RC input 7 | Receiver input | No direct input to Control; future explicit reverse-source configuration required |
| SRXL2 channel 1 | Zero-based index 0 | Throttle from Demand.throttle_percent |
| SRXL2 channel 7 | Zero-based index 6 | Calibrated reverse state, only with verified reverse settings and vehicle permission |

Control forces indexes 0 and 6 even if the calibration supplies other indexes.
Reverse-enabled packets have mask 0x41 and length 18 bytes; without verified
reverse settings the throttle-only mask is 1 and length is 16 bytes. Values in
these tests are synthetic, not validated Avian endpoints.

## Focused tests and evidence

Modified: libraries/AP_SpektrumSmartESC/tests/test_spektrum_control.cpp.
Added three cases to the existing 13-case suite:

1. ServoAileronAndRCInputsAreNotThrottleSources: a boundary fixture varies
   simulated SERVO1, RC1 and unconfigured RC7 through -100/0/100 while SERVO3
   demand remains 25%. Decoded throttle remains 16000 and reverse forward 2000.
   Changing simulated SERVO3 demand to 75% changes throttle to 46000.
2. ConfiguredReverseCannotWriteFlightSurfaces: an explicitly configured RC7
   fixture source is rejected without reverse permission. With permission,
   stopped/neutral evidence and dwell, channel 7 becomes synthetic reverse 60000;
   throttle remains zero 1000. Only channel mask 0x41 is encoded; simulated
   aileron/elevator/rudder outputs remain unchanged.
3. ActiveDemandNeverBypassesMain3Inhibit: after an 80% simulated command, removing
   MAIN3 inhibition immediately prevents frame generation and disables the route.
   Restoring a mock acknowledgement cannot automatically restart the route.

These fixtures explicitly copy simulated SERVO3 demand and an optional RC7 source
at the test boundary. They test the Control contract, not SRV_Channel,
RC_Channel, actual parameters, hardware GPIO state or a live vehicle adapter.
There is no claim of end-to-end servo/RC integration. Control has no output or
receiver-array access, and cannot write flight surfaces.

Existing cases were rerun for RC loss, ESC loss, failsafe, disarm, suppression,
invalid floats/ranges, missing exclusive ownership, stale/incorrect epochs,
neutral reauthorization, reverse stop/dwell and native diagnostic rejection of
motor frames. All 16 GTests passed; all 16 standalone cases passed ASan/UBSan.
The initial GTest build rejected float equality assertions under -Werror; these
were corrected to tolerance checks. Its failure log is retained. Final test builds
have no compiler warnings or sanitizer findings.

Reproduction: tools/validate-spktsmart-isolation.sh in the existing Linux Docker
build environment. Logs: logs/spktsmart-channel-isolation/{gtest-build.log,
gtest-control.log,control-build.log,control-sanitizers.log,inspection.log}.

## Physical output safety: current result and remaining blocker

No firmware runtime source, hwdef, parameters, IO image or RX-only transport was
changed. Phase 8 source-hash comparison found only the test file changed.
Plane/Copter were therefore not rebuilt. Inspection of the existing Phase 8
custom ELFs passed: RX write is the zero-return stub, bench arming veto remains,
physical TX/motor command implementation is absent, and the hardware gate is zero
with acquire returning false without peripheral writes. These are software checks
of existing binaries, not hardware tests or new builds.

Consequently AUX7 Smart motor commands cannot be active in the supplied firmware;
MAIN3's existing conventional behavior is unchanged. No operational digital motor
mode exists. The simulation refuses commands without fresh exclusive ownership,
MAIN3 PWM inhibition and AUX7 reservation, but those booleans are mock evidence.
A real persistent IO inhibit/acknowledgement coordinator across reset, local mixer,
failsafe and GPIO paths is NOT implemented. Future simultaneous-output prevention
cannot be claimed verified until that coordinator and live vehicle integration
are implemented and tested. There is no automatic fallback to active PWM.

Physical TX, throttle and reverse remain disabled. Nothing was flashed. Next:
retain electrical qualification gates, implement actual output ownership and a
reviewed final-demand adapter before any future digital motor mode, then test
real SERVO/RC parameter changes and reset/failsafe paths in SITL and hardware.
