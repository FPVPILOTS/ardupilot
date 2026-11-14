# Spektrum SRXL2 RUNNING recovery and commissioning review

Source of truth: SpektrumRC/SRXL2 `Source/spm_srxl.c`, `Source/spm_srxl.h`,
and `Examples/spm_srxl_config.h`. Refer to Rev K sections 7.1, 7.2,
7.6 and 7.7. **No production motor approval implied.**

## Implemented on separate experimental branch
- 0x21 `ESC 0x40 -> 0x00` while RUNNING is a spontaneous restart,
  **not** an ordinary heartbeat or an unconditional protocol error.
  A CRC-valid request starts disarmed-only discovery on the **same**
  UART/IO lease; old telemetry, idle proof and throttle authorization are
  invalidated. Armed, reverse, nonzero or repeated requests fail closed.
- 0x21 `0x40 -> 0x30` stays peer-alive, without motor authorization.
  Another source or malformed handshake remains an error.
- 0x80 DestID is a telemetry-forwarding destination, **not sender ID**:
  accept solicited decoded messages to `0x30` or `0x00`.
- 0x80 DestID `0xFF`: Spektrum's source explicitly requests the master
  re-send handshake. Only a time-correlated (15 ms window), CRC-valid,
  decoded request while DISARMED can initiate a reset to discovery.
- CRC-valid optional packet classes `0x41`, `0x50`, `0x55` are ignored
  (not executed), never used for identity, link freshness or motor control.
  Foreign `0xCD`, malformed, wrong addresses still fail closed.
- `SPK34` captures reject reason, packet type, destination and size
  and counts re-discovery / ignored packet types.
- `SPK35` separates UART software-ring overflow and pin-release failures
  from FE/NE/ORE. These counters can grow **after** an earlier F12 fault.

## Intentional unresolved qualifications
- The duplicated unit-0 IDs `FC=0x30`, `ESC=0x40` are not changed:
  actual physical hardware has already confirmed the handshake.
  No claim that this topology universally follows SRXL2 collision guidance.
- Baud negotiation remains **115200**. 400000 is supported by official
  Spektrum code, but enabling it requires proven duplex turnaround,
  transition after final broadcast, and an interrupt/DMA receive path
  in place of current 1kHz/16-byte FIFO polling.
- `0xCD` normal channel mask CH1 remains; omitted channels retain
  their previous values per the protocol. Real Avian CH7 forward/reverse
  wire polarity and ESC power-on behavior are unqualified.
- Encoder supports `0xCD` command 1 (failsafe) in **host simulation**,
  but experimental firmware still prevents a speculative physical failsafe
  packet until Avian CH1 stop endpoint and disconnect behavior are
  independently measured. Failure remains no-throttle, no-arm and fail
  closed. Do not substitute a guess for the physical motor cutoff.

## Hardware acceptance before any flight use
One battery cold boot with EDF motor phases disconnected and insulated;
normal DISARMED state. Look for native `SPK33 INIT`, handshake RP1,
`0xCD` CH1=768 neutral **candidate**, telemetry, then ongoing no-F12
status for extended testing. If F12: retrieve `SPK34 WHY/TYPE/DEST/LEN`
and `SPK35 OVER`. Test 0xFF request and 0x00 spontaneous handshake
with a mock vendor peer before enabling nonzero or reverse output.
Never bypass PreArm or wire the real EDF for this revision.
