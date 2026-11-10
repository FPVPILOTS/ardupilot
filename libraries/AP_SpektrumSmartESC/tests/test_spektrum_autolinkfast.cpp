/* SPDX-License-Identifier: GPL-3.0-or-later */
// Compiled with -DAP_SPEKTRUM_SMART_ESC_AUTOLINK_FAST_BOOT=1.
// No native IO, UART, PWM, motor control, or power-up hardware.
#include "../AP_SpektrumSmartESC_AutoSession.h"
#include <cassert>
#include <cstdio>
using Session = AP_SpektrumSmartESC_AutoSession;
using Auto = AP_SpektrumSmartESC_AutoConnect;

static void seal(uint8_t *p)
{
    const uint16_t crc = AP_SpektrumSmartESC::crc16(p, p[2] - 2);
    p[p[2] - 2] = uint8_t(crc >> 8);
    p[p[2] - 1] = uint8_t(crc);
}

int main()
{
    static_assert(Auto::STARTUP_LISTEN_MS == 50, "AutoLinkFast listen must be 50ms");
    static_assert(AP_SpektrumSmartESC_Reader::PREPARE_MS == 50,
                  "AutoLinkFast reader must be 50ms RX-first");
    Session s;
    s.enable(0, 0x40, 0xCAFE1234U);
    assert(s.tick(49000, true, true, true) == Auto::Action::NONE);
    assert(s.tick(50000, true, true, true) == Auto::Action::BEGIN_HANDSHAKE);
    uint8_t frame[80] {};
    assert(s.prepare(99000, frame, sizeof(frame), true) == 0);
    assert(s.prepare(100000, frame, sizeof(frame), true) == 14);
    assert(frame[0] == 0xA6 && frame[1] == 0x21 &&
           frame[3] == 0x30 && frame[4] == 0x40);
    assert(AP_SpektrumSmartESC::crc16(frame, 14) == 0);
    assert(s.status().successful == 0); // A query is NEVER a link.
    assert(s.sent(101000));
    uint8_t reply[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 0, 4,
                       0x78, 0x56, 0x34, 0x12, 0, 0};
    seal(reply);
    for (uint8_t b : reply) { s.feed(b, 103000); }
    assert(s.reader_status().responses == 1);
    assert(s.prepare(104000, frame, sizeof(frame), true) == 0); // RX silence
    assert(s.prepare(105000, frame, sizeof(frame), true) == 14);
    assert(frame[4] == 0xFF);
    assert(s.sent(106500));
    assert(s.tick(107000, true, true, false) == Auto::Action::NONE);
    assert(s.status().state == Auto::State::HANDSHAKE);
    assert(s.tick(108000, true, true, true) == Auto::Action::NONE);
    assert(s.status().state == Auto::State::LINKED);
    assert(s.status().successful == 1);

    // A silent, unpowered ESC may never count as a successful link.
    Session absent;
    absent.enable(0, 0x40, 0x1234U);
    assert(absent.tick(50000, true, true, true) == Auto::Action::BEGIN_HANDSHAKE);
    assert(absent.prepare(100000, frame, sizeof(frame), true) == 14);
    assert(absent.sent(101000));
    assert(absent.tick(5100000, true, true, true) == Auto::Action::NONE);
    assert(absent.status().state == Auto::State::BACKOFF);
    assert(absent.status().successful == 0);

    // An unsafe bench condition MUST still fail closed immediately.
    Session unsafe;
    unsafe.enable(0, 0x40, 0xABC);
    assert(unsafe.tick(50000, false, true, true) == Auto::Action::NONE);
    assert(unsafe.status().state == Auto::State::FAULT);

    std::puts("3 AutoLinkFast app-start timing/handshake/timeout safety tests passed (host only)");
    return 0;
}
