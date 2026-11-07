/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_LabHandshake.h"
#include <cassert>
#include <cstdio>

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
    std::puts("1 Phase 17C HSLab lease accessor test passed (simulation only)");
}
