# Spektrum Avian 130A / Pixhawk 6C — controlled EarlyApp startup investigation

## Status (2026-11-12)

This is **not production firmware**. The separate
`Pixhawk6C-SpktSmart-EarlyApp-IOInstall` image is non-armable and permits
only the existing CRC-checked 14-byte SRXL2 0x21 handshake. Do not fly it.
Motor phases must remain physically disconnected and insulated.

The ordinary `feature/spktsmart-bench6-contract` baseline at
`c047074f188f93b6383929ad7440396e1d4d4944` is unchanged.
This branch is an experiment and must not be merged without physical evidence.

## Protocol facts — Spektrum SRXL2 Specification, Rev K (2020-10-08)

- 3.3 V, half-duplex, 8N1; startup 115200 baud, optional 400000.
- The line is high impedance while idle; no simultaneous transmitters.
- Only one device on the bus should use a Unit ID (low nibble) of 0
  to prevent spontaneous-handshake collisions (§7.1.1 p.6).
- A non-master listens for 50 ms after reset; Unit ID 0 repeatedly
  broadcasts its 0x21 announcement with DestID=0 every 50 ms for the
  first >=200 ms until a handshake is received (§7.2.1 p.9).
- An addressed 0x21 reply and final 0x21 DestID=FF are separate requirements.
- A slave resets to 115200 and the handshake state when the master falls
  silent for 50 ms (§7.2.1 p.9).
- The 200 ms PWM fallback description applies to a **receiver** sharing a
  throttle PWM line, not proven for the Avian 130A specifically.
- The default master is an RF receiver/remote; Pixhawk-as-master is a
  custom topology requiring separate qualification (§6.2 p.5).

Current firmware uses FC=0x30 and ESC=0x40. **Both have Unit ID 0.**
Do not change either ID blindly: a physical 0x30<->0x40 exchange worked
during ESC-only repower, but startup collision remains an open hypothesis.
The standalone official Spektrum public library excludes the full master
implementation; conformance cannot be claimed from host mocks alone.

## Reproduced fault

- One battery to both devices: native Q/C/R successful, RXB=0 or
  a `UART_TRANSPORT_ERROR`, no linked peer.
- FC remains powered, ESC repowered: addressed 0x40 -> 0x30 handshake
  previously succeeded.
- AutoLinkFast init at ~3410ms FC uptime; 50ms listen + 50ms prepare
  begins well after the FC first started, *not* 100ms after battery-on.
- The prior `SPK25 FIRSTTC` field was **not** the first TC timestamp;
  it printed the most recent. The new experimental field is latched.

## What this experiment actually changes

1. The opt-in EarlyApp test board starts `spektrum_smart_esc.init()`
   immediately after `BoardConfig.init()` rather than near the end
   of `AP_Vehicle::setup()`; it still cannot run before ChibiOS/MCU boot.
2. It services the same 0x21-only AutoLinkFast reader for a bounded
   180ms after board config, then through the already-registered
   vehicle startup delay callback. Normal scheduler owns it after setup.
3. Reentrant delay callbacks are guarded.
4. The first genuine hardware UART TC is latched, and the first
   fail-closed gate is logged. No motor, 0xCD, reverse or parameter
   write requests are enabled.

The test does **not** guarantee a stable link during the remaining
ArduPlane setup. Nor does it prove the Avian's exact protocol-selection
window or that 0x30 is the correct persistent master ID.

### Diagnostic messages

- `SPK25 FAST INIT... L50 P50`: AutoLink init uptime and windows.
- `SPK25 INIT... FIRSTTC... NOW...`: app init, first actual native TX
  complete (zero means none), current system uptime in milliseconds.
- `SPK26 G<n> RX<t> ERR<e>`: first fail-closed gate, last native UART
  RX timestamp, current UART error counter.
- Gate meanings: 0 none, 1 unsafe bench precondition, 2 hardware
  diagnostics unavailable, 3 new UART error, 4 reader rejected true TC,
  5 startup-announcement gate rejected, 6 UART lease denied,
  7 handshake transmission denied.
- `SPK17 HS TX... RXB... CRC... RP...`: cumulative TX, RX bytes, CRC
  frames and valid targeted replies. `RP1` alone does NOT mean motor ready.

## Build only — do not flash without review

In Ubuntu WSL, preserve any uncommitted local changes:

```bash
cd ~/ardupilot-spkt
source ~/venv-ardupilot/bin/activate
git status -sb
git fetch origin feature/spktsmart-early-app-boot-20261112
git switch -c feature/spktsmart-early-app-boot-20261112 --track origin/feature/spktsmart-early-app-boot-20261112
bash libraries/AP_SpektrumSmartESC/tests/build_earlyapp_6c_installer.sh > ~/spkt-earlyapp-build.log 2>&1
result=$?
echo "EARLYAPP build exitcode: $result"
tail -n 40 ~/spkt-earlyapp-build.log
```

On a build failure, stop. A host CI pass alone is not a Pixhawk build.

### Before any physical test

- Review compiled binary, `.apj` board ID=56, IOMCU CRC=0x6DBBAD71,
  and the local build log; back up current parameters.
- Do not flash an image if the code/board/output has not passed review.
  This image may update the companion IOMCU.
- Disconnect and individually insulate all 3 EDF motor phases. Disconnect
  actuators for the bench. Keep dedicated emergency stop and bench arming
  inhibition. Do not change BEC/power topology or connect USB first.
- The `SPKT_ENABLE=1 SPKT_AUTO=1 SPKT_HSID=64 SPKT_MOTOR=0
  SPKT_TLM=0 SPKT_PROBE=0 SPKT_HS=0` settings apply only to the safe
  handshake lab; do not use motor parameters to force a link.

### Interpretation / next decision

1. No early true TC: find whether physical UART4 is initialized in time.
2. Early true TC but RXB0: inspect the actual on-wire waveform and ESC
   response timing before speculating about PWM fallback.
3. Early D00 announcement but RP0: analyze reply collision, master-ID
   election, RX restore, timing and CRC before sending more queries.
4. RP1 and linked from a cold battery start: validate repeated cold starts,
   ESC-only and FC-only reset; link alone does not authorize throttle.
5. A logic analyzer on AUX7/SRXL2 with battery-on reference and shared
   ground is the decisive electrical test. Avoid loading or driving
   the bus from the analyzer; ensure 3.3 V compatibility.

Do not deploy throttle/reverse until SRXL2 control data, actual stop
value, telemetry, failsafes, and electrical isolation are independently
verified on safe restrained hardware.
