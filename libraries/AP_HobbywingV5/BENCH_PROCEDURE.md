# Pixhawk6C + Hobbywing Platinum 150A V5.1 / Viper EDF receive-only bench procedure

This is a test plan, **not authorization to install unverified flight firmware**.

## Preparation and electrical verification

1. Identify exact Pixhawk6C hardware revision and actual Platinum 150A V5.1 variant. Record firmware versions. Back up existing ArduPilot params and original firmware.
2. Verify the official TELEM1 connector orientation/pin numbering with the Holybro hardware guide (do not infer numbering from MCU pin labels). Firmware maps TELEM1 to UART7: `PE7 UART7_RX`, `PE8 UART7_TX`.
3. Verify the ESC's *P* signal, ground, signal polarity, idle/peak levels and line electrical type with an appropriate logic analyzer/oscilloscope. Previously reported 3.0–3.3 V via DMM is insufficient by itself.
4. For first receive test connect **ESC P -> TELEM1 RX**, **ESC GND -> TELEM1 GND** only after confirming electrical compatibility. Do not connect programmer `+` to flight-controller header. No UART TX to ESC.
5. Disconnect motor phases / remove rotor blades for initial receive test. Keep independent cutoffs, normal disarmed behavior and all safety interlocks. Do not alter servo/governor settings.
6. Confirm TELEM1 not assigned to another device. USB provides Mission Planner connection.

## Software gate

- Pin full ArduPilot SHA, record build log and successful Pixhawk6C ArduPlane artifact.
- Run Python and C++ host regression tests plus firmware tests. Verify `SERVO_BLH_HWV5` metadata and default 0.
- Review firmware change to ensure no UART writing, DShot telemetry requests or motor-control interactions in selected branch. If build/tests fail, **do not flash**.
- With a successfully reviewed test image, set SERIAL1_PROTOCOL=16, SERIAL1_BAUD=115, SERVO_BLH_HWV5=1, then reboot.

## Initial listening observation

Look under Mission Planner Messages for lines like:

```text
HWV5 RX ok=... bad=... age=...ms
HWV5 RX Hz10=... dt=... max=... us
```

These are diagnostic formats, not real captures.

- If no messages: mode missing/inactive, UART unassigned, GCS unavailable, or SRV telemetry update hook not running.
- `ok>0`: frame type, length and CRC passed; verify actual values against external instruments.
- `bad>0`, `ok=0`: candidate frames failed CRC/type; capture raw bytes and waveform.
- `ok=0`, `bad=0`: no complete candidate frames; verify electrical/interface/startup assumptions. Do not send undocumented ESC programming commands.
- `Hz10=100` means measured 10.0 received **frames** per second, not proof that RPM sensor measurement internally updates at 10 Hz.
- `age=4294967295ms`: no valid frame seen; this is a diagnostic sentinel.
- `dt` and `max`: interarrival time in microseconds.

## Evidence log template

| Field | Captured value |
|---|---|
| ArduPilot and driver SHA | UNRECORDED |
| Pixhawk6C board rev/bootloader ID | UNRECORDED |
| Hobbywing ESC revision/firmware | UNRECORDED |
| Wiring/waveform trace | UNRECORDED |
| Raw valid 32-byte frames | UNRECORDED |
| Frame CRC + timing samples | UNRECORDED |
| Voltage/current/temp external comparison | UNRECORDED |
| Power-cycle recovery | UNRECORDED |
| Signal unplug/replug and stale detection | UNRECORDED |
| Regression tests / build / CI | UNRECORDED |

## Important limitations

- RPM withheld deliberately; Align tachometer remains primary governor source.
- Receiver does not poll or initialize ESC; if the physical Platinum does not stream passively, this test may show zero frames. Examine Rotorflight V5 half-duplex dialogue carefully before implementing any request.
- Parsed BEC values may be absent or not representative of an external servo BEC; standard MAVLink exposure requires further development.
- Existing serial16 still belongs to the AP_BLHeli library. Upstream review may require a dedicated driver or serial protocol to be accepted.
- Rotorflight simulator and synthetic tests are not a replacement for physical V5 evidence.
