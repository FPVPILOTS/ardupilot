/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "AP_SpektrumSmartESC_Motor.h"

// Real IO capability consumer. prepare() is asynchronous and disarmed-only;
// no receipt is issued until an actual matching readback is available.
class AP_SpektrumSmartESC_IOOwner : public AP_SpektrumSmartESC_Motor::Owner {
public:
    bool prepare(uint32_t nonce, uint32_t expected_crc);
    bool acquire(uint32_t epoch) override;
    AP_SpektrumSmartESC_Motor::Receipt receipt() const override;
    void release() override; // Stop capability polling; never unmute MAIN3.
private:
    uint32_t _nonce = 0, _epoch = 0;
};
