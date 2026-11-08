# Avian 130A — TurnLab profile and native startup qualification

This document is the referenced source-of-truth checklist for
`AP_SpektrumSmartESC_TurnLabProfile.h`. The **current source profile returns
`{}`**. That is deliberate: building the TurnLab firmware does **not**
qualify any actual ESC command.

## Confirmed by existing physical bench evidence

- Pixhawk 6C UART4 / AUX7 PA0, half duplex, 115200 baud.
- An ESC identified as device `0x40` participates in the real `0x21`
  handshake with flight-controller device `0x30`.
- `SPK17 SRXL2_LINK_ESTABLISHED` and regular `0x21` heartbeat frames have been
  observed in Mission Planner.
- A single empty-channel-mask `0xCD` trial was transmitted with true TC, but
  it had **no observed `0x80` response**. This does not establish accepted
  throttle control, STOP, or a particular telemetry reply policy.
- The built-in command mapping is **SRXL2 CH1** gas (index 0) and **SRXL2
  CH7** direction (index 6). This is **not** a requirement to reserve physical
  Pixhawk `SERVO7`. MAIN3 PWM output requires its own inhibit proof.

## Forward-only TurnLab profile

`AP_SpektrumSmartESC_TurnLab::Profile` is distinct from the production motor
profile, and contains:

| Field | Evidence that is required | Must be known for TurnLab? |
|---|---|---|
| `channels.throttle_channel == 0` | CH1 protocol position | yes |
| `channels.throttle_verified` | independently confirmed CH1 mapping | yes |
| `channels.zero` | measured digital value the actual Avian accepts as neutral/zero | yes |
| `channels.full` | measured digital end of accepted CH1 forward range | yes |
| `channels.reverse_channel == 6` | ESC configured to listen for direction on SRXL2 CH7 | yes |
| `forward_verified` | independently confirmed CH7 **forward** polarity | yes |
| `channels.forward` | digital CH7 value explicitly confirmed to mean forward | yes |
| `channels.reverse_verified` | qualification for reverse operation | **no** |
| `channels.reverse` | reverse endpoint | **no** |
| `electrical` | documented electrical path qualification | yes |
| `loss_behavior` | independently tested ESC behavior when commands/link are lost | yes |
| `io_crc` | actual deliberately installed matching IO image CRC | yes |

The first test sends CH1 neutral for 500 ms, CH1 **6% of the qualified
neutral-to-full range** for 500 ms and CH1 neutral for 500 ms, with CH7 kept
at the independently verified forward value. These limits are confined to
TurnLab; **the normal motor controller has no 6% cap**.

The SRXL2 specification uses 16-bit values with low two bits zero, and
`0x8000` as generic center; typical reference endpoints include `0x2AA0`
and `0xD554`. These are **NOT independently verified endpoint/polarity
values for this particular Avian 130A**. Never turn them into `*_verified`
flags by copying them from a sample packet. The Spektrum receiver port
mapping guide confirms CH1 throttle and independently routable reverse
channels, not the exact ESC CH7 forward threshold:
https://wiki.spektrumrc.com/spektrum/receiver-port-mapping-guide

## Pre-start checks enforced by firmware

1. Explicit volatile operator test consent and one run per boot.
2. Valid reviewed source profile and nonzero matching IOMCU CRC.
3. Async IOMCU inhibit nonce/readback, specifically suppressing MAIN3
   without touching SERVO7.
4. Current peer `0x40` evidence, UART4 RX ownership, idle bus, no new
   UART errors, disarmed emergency-stop state.
5. Native packet whitelist, 20-ms cadence, true hardware transmit completion
   and RX restoration.
6. On completion **or interruption**, the test releases its lease and remains
   consumed; a communication failure does **not** prove that a neutral
   command reached the ESC. Use independent power cutoff when necessary.

## Diagnostics / current limitations

When a TurnLab request is blocked, the firmware emits:
- `SPK22 PROFILE THx FWx IOx ELx LOSSx` — which source assertions are present.
- `SPK22 VALUES ZEROx FULLx CH7x` — values compiled into the profile.
- `SPK22 BLOCKED_PROFILE` if the profile fails qualification.

`SPK22 TC` proves completion of local serial output **only**.
`SPK22 RX/WIN/OUT` counts physical packets/candidates but is not proof of
ESC-applied throttle. Exact Avian endpoints, CH7 forward polarity, ESC loss
behavior, IO CRC and physical motor response remain unverified until
independently measured/confirmed.

## Building

```bash
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py
bash libraries/AP_SpektrumSmartESC/tests/build_phase18_motor_turn_lab.sh
```

Tests use synthetic 4000/64000/8000/56000 values **only inside host test
fixtures**. None of those values are asserted as physical Avian calibration.
