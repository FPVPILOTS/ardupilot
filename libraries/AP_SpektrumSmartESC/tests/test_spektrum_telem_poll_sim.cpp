/* SPDX-License-Identifier: GPL-3.0-or-later */
// PHASE17E-B: HOST-ONLY SIMULATION, NEVER LINKED INTO AIRCRAFT FIRMWARE.
//
// SRXL2 0xCD packs both ReplyID and potentially live throttle/channel data.
// A zero channel mask means "no channels updated", NOT "zero throttle",
// "disarmed", or "safe to connect the motor". Do not connect this preview
// encoder to UART4 or enable it as physical ESC output.
#include "../AP_SpektrumSmartESC.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using Parser = AP_SpektrumSmartESC;

struct OfflineBenchApproval {
    bool no_motor_connected = false;
    bool unarmable_image = false;
    bool single_esc = false;
    bool known_link = false;
    bool validated_electrical_interface = false;
    bool explicit_local_simulation = false;
};

class OfflinePollPreview {
public:
    bool preview(const OfflineBenchApproval &a, uint8_t *out, uint8_t &length)
    {
        length = 0;
        if (!a.no_motor_connected || !a.unarmable_image || !a.single_esc ||
            !a.known_link || !a.validated_electrical_interface ||
            !a.explicit_local_simulation || out == nullptr || _pending) {
            return false;
        }
        // SRXL2 0xCD command=0 (channel data), ReplyID=0x40 (ESC),
        // 1 RSSI byte, 2 frame losses, and 4-byte channel mask set to zero.
        // There are NO throttle values or channels in this preview packet.
        // Nevertheless, this is a CONTROL packet; real ESC behavior is
        // unknown, so this generated packet stays in this host test.
        const uint8_t payload[9] = {0x00, 0x40, 0, 0, 0, 0, 0, 0, 0};
        length = Parser::encode(0xCD, payload, sizeof(payload), out, 80);
        return length == 14;
    }

    // Mark a synthetic poll for simulation purposes only; no UART exists here.
    void simulate_sent(uint32_t time_ms)
    {
        _pending = true;
        _sent_ms = time_ms;
    }

    bool simulate_reply(const uint8_t *packet, uint8_t length, uint32_t time_ms,
                        Parser::Telemetry &result)
    {
        if (!_pending) {
            return false;
        }
        // One reply per poll, regardless of validity, so an old or bad reply
        // cannot be replayed as a good one.
        _pending = false;
        if (packet == nullptr || length != 22 || packet[0] != 0xA6 ||
            packet[1] != 0x80 || packet[2] != length || packet[3] != 0x30 ||
            uint32_t(time_ms - _sent_ms) >= 5 ||
            Parser::crc16(packet, length) != 0) {
            return false;
        }
        Parser::Telemetry decoded {};
        if (!Parser::decode_esc(packet + 4, 16, decoded) || decoded.valid == 0) {
            return false;
        }
        // A 0x80 frame contains a destination, but no source ID. Even a
        // successful decode is UNAUTHENTICATED laboratory evidence.
        result = decoded;
        return true;
    }

    void reset() { _pending = false; }

private:
    bool _pending = false;
    uint32_t _sent_ms = 0;
};

static uint8_t synthetic_esc_reply(uint8_t *out, uint8_t destination = 0x30,
                                   uint8_t sensor_id = 0x20)
{
    const uint8_t payload[17] = {
        destination, sensor_id, 0,
        0, 100,    // electrical RPM 1000
        9, 196,    // 25.00V
        1, 44,     // 30.0C
        0, 250,    // 2.50A
        0, 200,    // 20.0C BEC
        10,        // 1.0A BEC
        100,       // 5.0V BEC
        0xFF, 0xFF // no throttle/power readings
    };
    static_assert(sizeof(payload) == 17, "SRXL2 telemetry payload is 17 bytes");
    return Parser::encode(0x80, payload, sizeof(payload), out, 80);
}

static OfflineBenchApproval approved_simulation()
{
    OfflineBenchApproval a;
    a.no_motor_connected = true;
    a.unarmable_image = true;
    a.single_esc = true;
    a.known_link = true;
    a.validated_electrical_interface = true;
    a.explicit_local_simulation = true;
    return a;
}

static void refuse_without_every_lab_gate()
{
    OfflineBenchApproval a = approved_simulation();
    OfflinePollPreview p;
    uint8_t buf[80] {};
    uint8_t n = 0;
    a.no_motor_connected = false;
    assert(!p.preview(a, buf, n));
    a = approved_simulation();
    a.unarmable_image = false;
    assert(!p.preview(a, buf, n));
    a = approved_simulation();
    a.known_link = false;
    assert(!p.preview(a, buf, n));
    a = approved_simulation();
    a.single_esc = false;
    assert(!p.preview(a, buf, n));
    a = approved_simulation();
    a.validated_electrical_interface = false;
    assert(!p.preview(a, buf, n));
    a = approved_simulation();
    a.explicit_local_simulation = false;
    assert(!p.preview(a, buf, n));
}

static void preview_is_control_with_no_channel_updates()
{
    OfflinePollPreview p;
    uint8_t buf[80] {}, n = 0;
    assert(p.preview(approved_simulation(), buf, n));
    assert(n == 14 && buf[0] == 0xA6 && buf[1] == 0xCD);
    assert(buf[3] == 0 && buf[4] == 0x40);
    assert(Parser::crc16(buf, n) == 0);
    Parser::Channels channel {};
    assert(Parser::decode_channels(buf, n, channel));
    assert(channel.mask == 0);
    assert(channel.reply_id == 0x40);
    // No channel values exist here. Never interpret that as zero throttle.
}

static void synthetic_sensor_reply_is_a_candidate_not_esc_authentication()
{
    OfflinePollPreview p;
    Parser::Telemetry data {};
    uint8_t buf[80] {};
    const uint8_t n = synthetic_esc_reply(buf);
    assert(!p.simulate_reply(buf, n, 1001, data));
    p.simulate_sent(1000);
    assert(p.simulate_reply(buf, n, 1003, data));
    assert(data.electrical_rpm == 1000);
    assert(data.valid & Parser::VOLTAGE);
    assert(data.valid & Parser::CURRENT);
    assert(!p.simulate_reply(buf, n, 1004, data));
}

static void reject_wrong_destination()
{
    OfflinePollPreview p;
    Parser::Telemetry data {};
    uint8_t buf[80] {};
    const uint8_t n = synthetic_esc_reply(buf, 0x31);
    p.simulate_sent(1000);
    assert(!p.simulate_reply(buf, n, 1002, data));
}

static void reject_crc_error()
{
    OfflinePollPreview p;
    Parser::Telemetry data {};
    uint8_t buf[80] {};
    const uint8_t n = synthetic_esc_reply(buf);
    buf[12] ^= 1;
    p.simulate_sent(1000);
    assert(!p.simulate_reply(buf, n, 1002, data));
}

static void reject_unknown_sensor_type()
{
    OfflinePollPreview p;
    Parser::Telemetry data {};
    uint8_t buf[80] {};
    const uint8_t n = synthetic_esc_reply(buf, 0x30, 0x7E);
    p.simulate_sent(1000);
    assert(!p.simulate_reply(buf, n, 1002, data));
}

static void reject_late_response_and_reset()
{
    OfflinePollPreview p;
    Parser::Telemetry data {};
    uint8_t buf[80] {};
    const uint8_t n = synthetic_esc_reply(buf);
    p.simulate_sent(1000);
    assert(!p.simulate_reply(buf, n, 1005, data));
    p.simulate_sent(2000);
    p.reset();
    assert(!p.simulate_reply(buf, n, 2001, data));
}

int main()
{
    refuse_without_every_lab_gate();
    preview_is_control_with_no_channel_updates();
    synthetic_sensor_reply_is_a_candidate_not_esc_authentication();
    reject_wrong_destination();
    reject_crc_error();
    reject_unknown_sensor_type();
    reject_late_response_and_reset();
    std::puts("7 Phase 17E-B offline-only poll format and sensor reply tests passed");
}
