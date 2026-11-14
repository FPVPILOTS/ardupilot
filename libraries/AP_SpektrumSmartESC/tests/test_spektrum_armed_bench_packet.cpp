/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_Motor.h"
#include "../AP_SpektrumSmartESC_ChannelData.h"
#include "../AP_SpektrumSmartESC_ProductionProfile.h"
#include <cassert>
#include <cstdio>

// Synthetic physical transport: validates that an early ARM edge cannot
// turn a missing-peer session into any 0xCD motor command.
struct BenchPort : AP_SpektrumSmartESC_Bus::Port {
    bool complete = false;
    uint8_t type = 0;
    unsigned starts = 0;
    bool acquire() override { return true; }
    void relinquish() override {}
    bool simulated() const override { return false; }
    bool idle_high() const override { return true; }
    bool collision() const override { return false; }
    bool begin_tx(const uint8_t *p, uint8_t n) override {
        type = n > 1 ? p[1] : 0;
        ++starts; complete = false;
        return true;
    }
    bool transmission_complete() const override { return complete; }
    bool release_rx() override { return true; }
};
struct BenchOwner : AP_SpektrumSmartESC_Motor::Owner {
    AP_SpektrumSmartESC_Motor::Receipt r {};
    bool acquire(uint32_t epoch) override {
        r.epoch = epoch; r.io_boot = 123;
        r.main3_inhibited = r.failsafe_inhibited = r.aux7_reserved = true;
        return true;
    }
    AP_SpektrumSmartESC_Motor::Receipt receipt() const override { return r; }
    void release() override {}
};

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
    static_assert(AP_SpektrumSmartESC_Reader::PREPARE_MS == 2000,
                  "Host test retains 2-second default reader timing: "
                  "native ArmedBench must override it explicitly at runtime");
    static_assert(AP_SpektrumSmartESC_Reader::WINDOW_MS == 5000,
                  "ArmedBench must keep finite 5-second native handshake window");
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
    assert(profile.channels.zero == AP_SpektrumSmartESC_BenchRange::ZERO_CANDIDATE);
    assert(profile.channels.full == AP_SpektrumSmartESC_BenchRange::SIX_PERCENT_CANDIDATE);
    assert(profile.qualification.io_firmware_crc == 0x6DBBAD71U);

    uint16_t values[32] {};
    uint8_t packet[80] {};
    values[0] = AP_SpektrumSmartESC_BenchRange::ZERO_CANDIDATE;
    uint8_t n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                                0x01, values, packet, sizeof(packet));
    assert(n == 16 && Motor::control_packet(packet, n));

    values[0] = AP_SpektrumSmartESC_BenchRange::SIX_PERCENT_CANDIDATE;
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && Motor::control_packet(packet, n));

    values[0] = (AP_SpektrumSmartESC_BenchRange::ZERO_CANDIDATE-4);
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && !Motor::control_packet(packet, n));

    values[0] = (AP_SpektrumSmartESC_BenchRange::SIX_PERCENT_CANDIDATE+4);
    n = Channel::encode(Channel::Kind::Normal, 0x40, 0, 0,
                        0x01, values, packet, sizeof(packet));
    assert(n == 16 && !Motor::control_packet(packet, n));

    values[0] = AP_SpektrumSmartESC_BenchRange::ZERO_CANDIDATE;
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

    // Cold-start / FC-first: receive-only for 50ms, then actively request
    // the ESC on FC0 master (0x30). No 0xCD or ARM without reply/telemetry.
    {
        BenchPort port;
        BenchOwner owner;
        Motor motor(port, owner, profile.channels);
        assert(motor.start(profile.qualification, true, 0x40,
                           0x12345678U, 0U, 0U));
        Motor::Demand d {};
        owner.r.acquired_ms = d.acquired_ms = 49U;
        motor.update(d, 49000U, 49U);
        assert(port.starts == 0);
        owner.r.acquired_ms = d.acquired_ms = 50U;
        motor.update(d, 50000U, 50U);
        assert(port.starts == 1 && port.type == 0x21);
        assert(motor.status().controls == 0);
        port.complete = true;
        owner.r.acquired_ms = d.acquired_ms = 52U;
        motor.update(d, 52000U, 52U);
        assert(motor.status().tx_frames == 1);
        assert(motor.reader_status().first_targeted_tc_ms == 52U);
        assert(motor.status().controls == 0);

        // Physically observed FC0/ESC0 handshake vector, synthetically
        // reproduced here using the same parser (not fake readiness).
        const uint8_t payload[] = {0x40, 0x30, 10, 0, 0,
                                   0x78, 0x56, 0x34, 0x12};
        uint8_t frame[80] {};
        const uint8_t n = AP_SpektrumSmartESC::encode(
            0x21, payload, sizeof(payload), frame, sizeof(frame));
        assert(n == 14);
        for (unsigned i = 0; i < n; i++) {
            motor.feed(frame[i], 53000U);
        }
        assert(motor.reader_status().responses == 1);
        owner.r.acquired_ms = d.acquired_ms = 54000U / 1000U;
        motor.update(d, 54000U, 54U);
        assert(port.starts == 1); // 2-ms half-duplex RX silence.
        owner.r.acquired_ms = d.acquired_ms = 56000U / 1000U;
        motor.update(d, 56000U, 56U);
        assert(port.starts == 2 && port.type == 0x21);
        port.complete = true;
        owner.r.acquired_ms = d.acquired_ms = 58000U / 1000U;
        motor.update(d, 58000U, 58U);
        owner.r.acquired_ms = d.acquired_ms = 59000U / 1000U;
        motor.update(d, 59000U, 59U);
        assert(motor.status().state == Motor::State::IDLE);
        assert(motor.status().controls == 0);
        assert(!motor.ready_to_arm(59U, 59000U));
    }

    // Native object receives a real Plane ARM *before* the ESC responds.
    // Only the 0x21 handshake is permitted, even after a finite retry.
    BenchPort port;
    BenchOwner owner;
    Motor motor(port, owner, profile.channels);
    assert(motor.start(profile.qualification, true, 0x40,
                       0x12345678U, 0U, 0U));
    Motor::Demand demand {};
    demand.armed = true;
    demand.rc_ok = demand.control_ok = true;
    demand.magnitude = 0.06f; // Raised after ARM must NOT resume on late link.
    owner.r.acquired_ms = demand.acquired_ms = 10U;
    motor.update(demand, 10000U, 10U);
    assert(motor.status().bench_monitor);
    assert(motor.status().fault == Motor::Fault::NONE);
    assert(motor.status().controls == 0);
    owner.r.acquired_ms = demand.acquired_ms = 2010U;
    motor.update(demand, 2010000U, 2010U);
    assert(port.starts == 1 && port.type == 0x21);
    port.complete = true;
    owner.r.acquired_ms = demand.acquired_ms = 2012U;
    motor.update(demand, 2012000U, 2012U);
    assert(motor.status().tx_frames == 1);
    owner.r.acquired_ms = demand.acquired_ms = 9000U;
    motor.update(demand, 9000000U, 9000U);
    assert(motor.status().fault == Motor::Fault::NONE);
    assert(motor.status().controls == 0);
    owner.r.acquired_ms = demand.acquired_ms = 13001U;
    motor.update(demand, 13001000U, 13001U);
    assert(motor.status().bench_link_retries == 1U);
    assert(motor.status().controls == 0);
    demand.armed = false;
    demand.magnitude = 0.0f;
    owner.r.acquired_ms = demand.acquired_ms = 13002U;
    motor.update(demand, 13002000U, 13002U);
    assert(!motor.status().bench_monitor);

    // The unlinked bench ARM policy may never send a nonzero CH1 packet.
    // Actual virtual-ARM transition and retry safety are source-gated to
    // this separate compile variant; ordinary Motor tests remain unchanged.
    assert(profile.channels.reverse_verified == false);
    assert(profile.channels.full == AP_SpektrumSmartESC_BenchRange::SIX_PERCENT_CANDIDATE);
    std::puts("ArmedBench bounded CH1-only whitelist tests passed (host; no hardware)");
}
