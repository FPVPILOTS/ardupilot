/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#if !defined(SPKTSMART_STANDALONE_TEST) && !defined(SPKTSMART_UART4_REGISTER_TEST) && !defined(SPKTSMART_RX_REGISTER_TEST)
#include "AP_SpektrumSmartESC_config.h"
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_LAB_ONLY
#define AP_SPEKTRUM_SMART_ESC_LAB_ONLY 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#define AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
#define AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_POLL_LAB
#define AP_SPEKTRUM_SMART_ESC_POLL_LAB 0
#endif
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB && !AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB
#error "SRXL2 PollLab requires the bounded AutoLab handshake"
#endif
#if AP_SPEKTRUM_SMART_ESC_POLL_LAB && AP_SPEKTRUM_SMART_ESC_LAB_ONLY
#error "Physical 0xCD PollLab must not mix with disconnected waveform lab"
#endif
#if AP_SPEKTRUM_SMART_ESC_AUTO_HANDSHAKE_LAB && !AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#error "Automatic handshake LAB requires the strict HSLab packet whitelist"
#endif
#if AP_SPEKTRUM_SMART_ESC_HANDSHAKE_LAB
#if AP_SPEKTRUM_SMART_ESC_LAB_ONLY
#error "Handshake laboratory and disconnected waveform laboratory are separate images"
#endif
#if defined(AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED) && AP_SPEKTRUM_SMART_ESC_HARDWARE_TX_VALIDATED
#error "Experimental handshake authorization is not electrical qualification"
#endif
#if defined(AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED) && AP_SPEKTRUM_SMART_ESC_MOTOR_TX_VALIDATED
#error "Handshake laboratory must never enable motor transmission"
#endif
#endif

// No SRXL2 header or channel-bearing data can enter the disconnected-line lab.
class AP_SpektrumSmartESC_Lab {
public:
    static bool packet(const uint8_t *p, uint8_t n)
    {
        return p != nullptr && n == 4 && p[0] == 0x55 && p[1] == 0xAA && p[2] == 0x55 && p[3] == 0xAA;
    }
};
