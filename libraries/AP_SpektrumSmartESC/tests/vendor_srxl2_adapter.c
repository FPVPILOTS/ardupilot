/*
 * Host-only bridge to the unmodified SpektrumRC SRXL2 slave parser (MIT).
 * This file is not included in Pixhawk firmware or any flight binary.
 */
#include "spm_srxl.h"
#include <string.h>

unsigned vendor_received_controls = 0;
unsigned vendor_received_failsafes = 0;
unsigned vendor_emitted_handshakes = 0;
unsigned vendor_emitted_telemetry = 0;
uint32_t vendor_received_mask = 0;
uint16_t vendor_received_values[32] = {0};

bool vendor_init(void)
{
    memset(&srxlChData, 0, sizeof(srxlChData));
    vendor_received_controls = 0;
    vendor_received_failsafes = 0;
    vendor_emitted_handshakes = 0;
    vendor_emitted_telemetry = 0;
    vendor_received_mask = 0;
    memset(vendor_received_values, 0, sizeof(vendor_received_values));
    /* Run the vendor parser as the ESC-class device 0x40, NOT as a master.
     * This tests packet compatibility; it does not emulate Avian motor logic. */
    return srxlInitDevice(0x40, 10, SRXL_DEVINFO_NO_RF, 0x12345678U) &&
           srxlInitBus(0, 0, SRXL_BAUD_115200);
}

bool vendor_parse(const uint8_t *packet, uint8_t size)
{
    uint8_t copy[SRXL_MAX_BUFFER_SIZE];
    if (packet == 0 || size < 5 || size > sizeof(copy)) {
        return false;
    }
    memcpy(copy, packet, size);
    return srxlParsePacket(0, copy, size);
}
