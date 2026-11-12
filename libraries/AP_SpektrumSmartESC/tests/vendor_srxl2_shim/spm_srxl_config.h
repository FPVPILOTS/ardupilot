/*
 * Host-only callback adapter for SpektrumRC/SRXL2 at
 * 6251dd14fbc95f262ecbc22e38ad39a8b907e411 (MIT).
 * No aircraft firmware links these callbacks.
 */
#ifndef AP_SPKT_VENDOR_SRXL_CONFIG_H
#define AP_SPKT_VENDOR_SRXL_CONFIG_H
#include <stdint.h>
#include <stdbool.h>
#define SRXL_NUM_OF_BUSES 1
#define SRXL_CRC_OPTIMIZE_MODE SRXL_CRC_OPTIMIZE_SIZE
#define SRXL_SUPPORTED_BAUD_RATES 0

extern unsigned vendor_received_controls;
extern unsigned vendor_received_failsafes;
extern unsigned vendor_emitted_handshakes;
extern unsigned vendor_emitted_telemetry;
extern uint32_t vendor_received_mask;
extern uint16_t vendor_received_values[32];

static inline void srxlChangeBaudRate(uint8_t uart, uint32_t baudRate)
{
    (void)uart; (void)baudRate;
}
static inline void srxlSendOnUart(uint8_t uart, uint8_t *data, uint8_t length)
{
    (void)uart;
    if (data != 0 && length >= 5) {
        if (data[1] == SRXL_HANDSHAKE_ID) { vendor_emitted_handshakes++; }
        if (data[1] == SRXL_TELEM_ID) { vendor_emitted_telemetry++; }
    }
}
static inline void srxlFillTelemetry(SrxlTelemetryData *data)
{
    /* An all-zero dummy telemetry payload is used only for host parsing. */
    for (unsigned i = 0; i < sizeof(data->raw); i++) { data->raw[i] = 0; }
}
static inline void srxlReceivedChannelData(SrxlChannelData *data, bool failsafe)
{
    vendor_received_controls++;
    vendor_received_failsafes += failsafe ? 1U : 0U;
    vendor_received_mask = data->mask;
    for (unsigned i = 0; i < 32; i++) {
        vendor_received_values[i] = srxlChData.values[i];
    }
}
static inline bool srxlOnBind(SrxlFullID device, SrxlBindData info)
{
    (void)device; (void)info;
    return false;
}
static inline void srxlOnVtx(SrxlVtxData *data)
{
    (void)data;
}
static inline void srxlEnterCriticalSection(void) {}
static inline void srxlExitCriticalSection(void) {}
#endif
