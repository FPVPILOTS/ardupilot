/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_RunningPolicy.h"
#include <cassert>
#include <cstdio>
using Policy = AP_SpektrumSmartESC_RunningPolicy;
using Action = Policy::Action;

int main()
{
    uint8_t hs[] = {0xA6, 0x21, 14, 0x40, 0x30, 10, 0, 4, 1, 2, 3, 4, 0, 0};
    assert(Policy::classify(hs, sizeof(hs), 0x30, 0x40) == Action::PEER_ALIVE);
    hs[4] = 0;
    assert(Policy::classify(hs, sizeof(hs), 0x30, 0x40) == Action::RESTART);
    hs[4] = 0x31;
    assert(Policy::classify(hs, sizeof(hs), 0x30, 0x40) == Action::REJECT);
    hs[4] = 0x30; hs[3] = 0x41;
    assert(Policy::classify(hs, sizeof(hs), 0x30, 0x40) == Action::REJECT);
    hs[3] = 0x40; hs[6] = 2;
    assert(Policy::classify(hs, sizeof(hs), 0x30, 0x40) == Action::REJECT);
    uint8_t telemetry[22] = {0xA6, 0x80, 22, 0x30};
    assert(Policy::classify(telemetry, sizeof(telemetry), 0x30, 0x40) == Action::SENSOR);
    telemetry[3] = 0x00;
    assert(Policy::classify(telemetry, sizeof(telemetry), 0x30, 0x40) == Action::SENSOR);
    telemetry[3] = 0xFF;
    assert(Policy::classify(telemetry, sizeof(telemetry), 0x30, 0x40) == Action::SENSOR_RESTART);
    telemetry[3] = 0x31;
    assert(Policy::classify(telemetry, sizeof(telemetry), 0x30, 0x40) == Action::REJECT);
    telemetry[2] = 21;
    assert(Policy::classify(telemetry, sizeof(telemetry), 0x30, 0x40) == Action::REJECT);
    uint8_t opt[] = {0xA6, 0x55, 6, 0, 0, 0};
    assert(Policy::classify(opt, sizeof(opt), 0x30, 0x40) == Action::IGNORE_OPTIONAL);
    opt[1] = 0x50;
    assert(Policy::classify(opt, sizeof(opt), 0x30, 0x40) == Action::IGNORE_OPTIONAL);
    opt[1] = 0x41;
    assert(Policy::classify(opt, sizeof(opt), 0x30, 0x40) == Action::IGNORE_OPTIONAL);
    opt[1] = 0xCD;
    assert(Policy::classify(opt, sizeof(opt), 0x30, 0x40) == Action::REJECT);
    std::puts("14 strict SRXL2 RUNNING packet policy checks passed");
}
