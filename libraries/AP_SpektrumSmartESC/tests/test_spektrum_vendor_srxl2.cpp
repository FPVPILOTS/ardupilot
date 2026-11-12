/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Independent interoperability test:
 * ArduPilot wire encoder -> actual SpektrumRC/SRXL2 parser.
 *
 * The vendor implementation is compiled from pinned upstream MIT source,
 * without SRXL_INCLUDE_MASTER_CODE (Spektrum does not publish that code).
 * These synthetic channel values do NOT certify Avian ESC throttle endpoints,
 * reverse polarity, or safe physical motion. This test has NO UART/HAL.
 */
#include "../AP_SpektrumSmartESC.h"
#include "../AP_SpektrumSmartESC_ChannelData.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" {
bool vendor_init(void);
bool vendor_parse(const uint8_t *packet, uint8_t size);
extern unsigned vendor_received_controls;
extern unsigned vendor_received_failsafes;
extern unsigned vendor_emitted_handshakes;
extern unsigned vendor_emitted_telemetry;
extern uint32_t vendor_received_mask;
extern uint16_t vendor_received_values[32];
}

using Codec = AP_SpektrumSmartESC_ChannelData;
using SRXL = AP_SpektrumSmartESC;

static void handshake(uint8_t destination)
{
    const uint8_t payload[] = {
        0x30, destination, 10, 0, 0, 0x56, 0x34, 0x12, 0x01
    };
    uint8_t packet[80] {};
    const uint8_t size = SRXL::encode(0x21, payload, sizeof(payload), packet, sizeof(packet));
    assert(size == 14 && vendor_parse(packet, size));
}

static void channels(Codec::Kind kind, uint8_t reply, uint32_t mask,
                     uint16_t ch1, uint16_t ch7, unsigned expected_control_count)
{
    uint16_t values[32] {};
    values[0] = ch1;
    values[6] = ch7;
    uint8_t packet[80] {};
    const uint8_t size = Codec::encode(kind, reply, 67, 3, mask, values,
                                       packet, sizeof(packet));
    assert(size == uint8_t(14 + 2 * ((mask & 1U) != 0U) + 2 * ((mask & 0x40U) != 0U)));
    assert(SRXL::crc16(packet, size) == 0);
    assert(packet[0] == 0xA6 && packet[1] == 0xCD);
    assert(packet[3] == uint8_t(kind) && packet[4] == reply);
    assert(vendor_parse(packet, size));
    assert(vendor_received_controls == expected_control_count);
    assert(vendor_received_mask == mask);
    if (mask & 1U) { assert(vendor_received_values[0] == ch1); }
    if (mask & 0x40U) { assert(vendor_received_values[6] == ch7); }
}

int main()
{
    assert(vendor_init());

    // FC 0x30 addresses ESC 0x40, then broadcasts the 115200-baud result.
    handshake(0x40);
    handshake(0xFF);
    assert(vendor_received_controls == 0);
    assert(vendor_emitted_handshakes >= 1);

    // CH1=normal throttle and CH7=synthetic forward level.
    // Spektrum defines channel masks and layout, not the Avian's direction polarity.
    channels(Codec::Kind::Normal, 0x40, 0x41, 768, 20000, 1);
    assert(vendor_emitted_telemetry == 1); // ReplyID 0x40 selects ESC response.

    // Low throttle before changing synthetic CH7 direction.
    channels(Codec::Kind::Normal, 0, 0x41, 768, 40000, 2);
    assert(vendor_emitted_telemetry == 1); // ReplyID 0 requests no response.
    channels(Codec::Kind::Normal, 0x40, 0x41, 4608, 40000, 3);
    assert(vendor_emitted_telemetry == 2);

    // Failsafe is a separate command type; vendor callback sees the flag.
    channels(Codec::Kind::Failsafe, 0, 0x41, 768, 20000, 4);
    assert(vendor_received_failsafes == 1);

    // A CRC-corrupted frame must not update the vendor channel state.
    uint16_t values[32] {};
    values[0] = 768;
    uint8_t packet[80] {};
    const uint8_t size = Codec::encode(Codec::Kind::Normal, 0, 0, 0, 1,
                                       values, packet, sizeof(packet));
    assert(size == 16);
    packet[size - 1] ^= 0x80;
    assert(!vendor_parse(packet, size));
    assert(vendor_received_controls == 4);

    std::puts("VENDOR SRXL2 INTEROP PASS: handshake, CH1/CH7, replyID, failsafe, CRC");
    return 0;
}
