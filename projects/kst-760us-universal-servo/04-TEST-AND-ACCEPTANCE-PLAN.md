# Verification plan and evidence checklist

**Do not flash or fly based on uncompiled source changes.** Follow the gates in sequence, record commit SHAs and attach logs.

## Test levels

### A — Static/compile
- Compile `./waf configure --board sitl && ./waf copter`; compile for exact confirmed Pixhawk4 board.
- Build Plane and Rover to catch shared SRV_Channel compile/regression issues.
- Verify generated parameter metadata exposes `H_SW_PWM_MODE` and 500us lower metadata ranges.
- Audit changed files against base commit and check no unintended ESC/throttle changes.

### B — Pure servo-map unit tests (C++ test harness preferred)
Cases: symmetric 1500us legacy; 760us 560/760/960; asymmetric 540/760/950; reversed; distinct widths across 3 swash outputs; -1, -0.5, 0, +0.5, +1; clipping beyond limits; invalid MIN>=TRIM, TRIM>=MAX, missing function, duplicate assigned function, persistence after reload. Expected values and rounding exact.

### C — SITL Traditional Heli
- H3_120 and H3_140, generic H3 including phase, H1 and H4 swash types.
- H_SV_MAN 0/1/2/3/4; H_COL_MIN/MAX/ANG_MIN/ANG_MAX, ZERO_THRST and COL_LAND_MIN.
- Swash linearization OFF/ON; collective direction and channel reversal permutations.
- Full simultaneous roll+pitch+collective commands, saturation and physical limits; zero-collective and hover midpoints.
- Tail servo output, H_COLYAW compensation, external gyro gain and RSC modes unchanged.
- Disarmed loop (reinitialization), arming/disarming, reboot, RC failsafe, transmitter loss and motor interlock.
- Record expected and actual `SERVOx`, output function assignments and RCOU; automated comparison baseline vs mode 1 and legacy mode 0.

### D — Hardware measurement (Pixhawk4)
- First measure PWM with servo disconnected using PWM capture gear/logic analyzer/microcontroller before connecting high-value servo.
- Sweep command and measure MIN/TRIM/MAX actual pulse high time in us and 560Hz frame period (~1785.7us).
- Confirm channel/timer bank interactions; no unintended ESC throttle frequency/protocol change; verify safety switch/disarm/failsafe output.
- Check electric wiring: approved servo power distribution, common signal ground, BEC capacity under simultaneous loads, and power input isolation to Pixhawk.
- Only connect servo after electrical validation; no motor/rotor propulsion during first movement test.

### E — Mechanical setup
- Mechanical setup and endpoint tests with ESC/rotor drive disabled and safe blade clearance.
- Check linkage travel for cyclic+collective corner combinations, reversed servo direction, swash leveling at electrical neutral and blade pitch at H_SV_MAN 2/3/4.
- Tail motion and gyro compensation direction; monitor servo temperature/current and end-stop stall.
- Record measured blade pitch degrees, swash travel, and settings; restore H_SV_MAN=0 and H_SV_TEST=0.

## Release acceptance checklist

- [ ] All source changes reviewed and traceable to requirement
- [ ] Clean Copter, Plane, Rover compile; reproducible toolchain environment
- [ ] C++ helper unit tests and SITL cases pass
- [ ] Parameter values retain across reboot
- [ ] Pre-arm rejects all unsafe/invalid servo ranges and missing mappings
- [ ] Legacy mode 0 regression verified
- [ ] 560Hz actual waveform and timer behavior measured, no ESC interference
- [ ] Blade angles and mechanical bounds measured, failsafe/interlock checked
- [ ] Versioned firmware binary, SHA and exact target documented
- [ ] Backup .param and known-good rollback firmware stored
- [ ] Written bench-test signoff; flight test only after separate airworthiness assessment

## Evidence table template

| Test ID | Commit SHA | Firmware + Board | Configuration | Expected | Actual | Evidence path | PASS/FAIL |
|---|---|---|---|---|---|---|---|
| T-001 | pending | SITL Copter | mode=0 | legacy behavior | pending | pending | PENDING |
| T-002 | pending | SITL Copter | 760us H3 center | 760us | pending | pending | PENDING |
| T-003 | pending | Pixhawk4 | 560Hz electrical | 1785.7us period | pending | pending | PENDING |
