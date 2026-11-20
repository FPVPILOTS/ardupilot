#include "../../libraries/AP_HobbywingV5/Protocol.h"
#include <assert.h>
#include <math.h>
#include <string.h>

using namespace HobbywingV5;

static void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
static void update_crc(uint8_t *data) { put16(data + 30, crc16_modbus(data, 30)); }

int main()
{
    assert(crc16_modbus(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0x4B37);
    uint8_t packet[32]{};
    packet[0] = 0xFD; packet[1] = 1; packet[3] = 3;
    put16(packet + 4, 0x5C30);
    packet[9] = 65; packet[12] = 2;
    put16(packet + 13, 1200); put16(packet + 15, 252); put16(packet + 17, 315);
    packet[19] = 43; packet[20] = 34; packet[21] = 40;
    packet[22] = 84; packet[23] = 17;
    update_crc(packet);

    Sample out{};
    assert(decode_telemetry(packet, sizeof(packet), out));
    assert(out.throttle_percent == 65 && out.fault_raw == 2);
    assert(out.rpm_field_x10 == 12000);
    assert(fabsf(out.voltage_v - 25.2f) < .001f);
    assert(fabsf(out.current_a - 31.5f) < .001f);
    assert(out.esc_temp_c == 43 && out.bec_temp_c == 34 && out.motor_temp_c == 40);
    assert(fabsf(out.bec_voltage_v - 8.4f) < .001f);
    assert(fabsf(out.bec_current_a - 1.7f) < .001f);
    Sample preserved = out;
    assert(!decode_telemetry(nullptr, 32, out));
    assert(!decode_telemetry(packet, 31, out));
    assert(!decode_telemetry(packet, 33, out));
    packet[15] ^= 1;
    assert(!decode_telemetry(packet, 32, out));
    packet[15] ^= 1;
    packet[4] = 0;
    update_crc(packet);
    assert(!decode_telemetry(packet, 32, out));
    put16(packet + 4, 0x5C30);
    packet[0] = 0xFE;
    update_crc(packet);
    assert(decode_telemetry(packet, 32, out));
    assert(memcmp(&out, &preserved, sizeof(out)) == 0);
    return 0;
}
