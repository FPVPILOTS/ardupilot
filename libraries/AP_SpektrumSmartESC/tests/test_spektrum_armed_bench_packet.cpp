/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Motor.h"
#include "../AP_SpektrumSmartESC_ChannelData.h"
#include "../AP_SpektrumSmartESC_ProductionProfile.h"
#include <cassert>
#include <cstdio>

// Separate *host* binary built with ARMED_BENCH_EXPERIMENTAL=1.
// The compile-time bench transport gate is necessary but by itself is never
// sufficient to start a physical motor; source-reviewed profile and IOMCU
// qualification remain mandatory.
int main()
{
    using Motor = AP_SpektrumSmartESC_Motor;
    using Channel = AP_SpektrumSmartESC_ChannelData;
    static_assert(AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL == 1,
                  "Compile this suite only with the isolated bench flag");
    assert(Motor::physical_control_available());
    const auto profile = spektrum_production_profile();
    // A selected *bench* build may be syntactically eligible for an
    // explicit disarmed session, but cannot claim physical certification.
    assert(profile.valid());
    assert(!profile.operator_qualified);
    assert(!profile.channels.throttle_verified);
    assert(!profile.channels.reverse_verified);
    assert(!profile.qualification.electrical);
    assert(!profile.qualification.protocol);
    assert(!profile.qualification.endpoints);
    assert(!profile.qualification.esc_loss_behavior);
    assert(!profile.qualification.reverse_settings);
    assert(profile.esc_id == 0x40);
    assert(profile.channels.zero == 768);
    assert(profile.channels.full == 4608);
    assert(profile.qualification.io_firmware_crc == 0x6DBBAD71U);

    uint16_t values[32] {};
    uint8_t packet[80] {};
    values[0] = 768;
    uint8_t n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                                0x01, values, packet, sizeof(packet));
    assert(n == 16 && Motor::control_packet(packet, n));

    values[0] = 4608;
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && Motor::control_packet(packet, n));

    values[0] = 764;
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && !Motor::control_packet(packet, n));

    values[0] = 4612;
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && !Motor::control_packet(packet, n));

    values[0] = 768;
    values[6] = 32768;
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x41, values, packet, sizeof(packet));
    assert(n == 18 && !Motor::control_packet(packet, n));

    n = Channel::encode(Channel::Kind::Failsafe, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && !Motor::control_packet(packet, n));

    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16);
    packet[12] = 0x01;
    assert(!Motor::control_packet(packet, n));
    assert(!Motor::control_packet(packet, 0));
    assert(!Motor::control_packet(nullptr, 16));

    std::puts("ArmedBench bounded CH1-only whitelist tests passed (host; no hardware)");
}
