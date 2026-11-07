/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_LabHandshake.h"
#include <cassert>
#include <cstdio>

static void seal(uint8_t *p)
{
    const uint16_t crc = AP_SpektrumSmartESC::crc16(p, 12);
    p[12] = uint8_t(crc >> 8);
    p[13] = uint8_t(crc);
}


int main()
{
    using Lease = AP_SpektrumSmartESC_LabHandshake::Lease;
    Lease original;
    assert(!original.used());
    assert(original.start(0x40, 0x1234U, 100U, true));
    assert(original.used());
    assert(original.active(101U));
    assert(!original.start(0x40, 0x1234U, 102U, true));
    original.close();
    assert(original.used());
    assert(!original.active(103U));
    // The original HSLab lease never re-arms itself. Only the separate
    // AutoLab native owner may replace an expired, released lease object.
    Lease replaced;
    assert(!replaced.used());
    assert(replaced.start(0x40, 0x1234U, 10000U, true));
    assert(replaced.used());
    // Early 0x21 emission is forbidden without a matching, separately
    // CRC-validated 0x40->0 unsolicited startup announcement.
    uint8_t hello[] = {0xA6, 0x21, 14, 0x40, 0, 10, 0, 4,
                       1, 2, 3, 4, 0, 0};
    seal(hello);
    uint8_t directed[] = {0xA6, 0x21, 14, 0x30, 0x40, 10, 0, 0,
                          0x34, 0x12, 0, 0, 0, 0};
    seal(directed);
    Lease early;
    assert(early.start(0x40, 0x1234U, 100U, true));
    assert(!early.take(directed, sizeof(directed), 110U));
    hello[13] ^= 1;
    assert(!early.authorize_startup_announcement(hello, sizeof(hello), 111U, 111U));
    hello[13] ^= 1;
    hello[3] = 0x41;
    seal(hello);
    assert(!early.authorize_startup_announcement(hello, sizeof(hello), 112U, 112U));
    hello[3] = 0x40;
    seal(hello);
    assert(early.authorize_startup_announcement(hello, sizeof(hello), 113U, 113U));
    assert(early.take(directed, sizeof(directed), 115U));
    // The same native 0x21-only lease allows one final broadcast, but not
    // another transmission after the confirmation packet.
    directed[4] = 0xFF;
    seal(directed);
    assert(early.take(directed, sizeof(directed), 120U));
    assert(!early.take(directed, sizeof(directed), 121U));

    Lease expired;
    assert(expired.start(0x40, 0x1234U, 100U, true));
    assert(expired.authorize_startup_announcement(hello, sizeof(hello), 110U, 110U));
    directed[4] = 0x40;
    seal(directed);
    assert(!expired.take(directed, sizeof(directed), 186U));

    Lease stale;
    assert(stale.start(0x40, 0x1234U, 100U, true));
    assert(!stale.authorize_startup_announcement(hello, sizeof(hello), 105U, 130U));
    assert(!stale.authorize_startup_announcement(hello, sizeof(hello), 99U, 101U));

    Lease ordinary;
    assert(ordinary.start(0x40, 0x1234U, 100U, false));
    assert(!ordinary.authorize_startup_announcement(hello, sizeof(hello), 110U, 110U));

    std::puts("2 Phase 17D HSLab lease/accessor and early-startup native-gate tests passed (simulation only)");
}
