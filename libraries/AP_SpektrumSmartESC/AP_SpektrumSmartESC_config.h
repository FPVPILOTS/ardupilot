/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <AP_HAL/AP_HAL_Boards.h>
#ifndef AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED
#define AP_SPEKTRUM_SMART_ESC_DIAGNOSTICS_ENABLED 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_EARLY_APP_BOOT
#define AP_SPEKTRUM_SMART_ESC_EARLY_APP_BOOT 0
#endif
#ifndef AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT
#define AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT 0
#endif
#if AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT && AP_SPEKTRUM_SMART_ESC_EARLY_APP_BOOT
#error "Native and AutoLab SRXL2 early boot are mutually exclusive"
#endif

// Separate experimental MOTOR-CAPABLE bench: reuses the early native
// power-on handshake, but NEVER disables ordinary Plane ARM checks.
// It is NOT flight firmware and MUST use the special MAIN3-inhibited IO.
#ifndef AP_SPEKTRUM_SMART_ESC_EARLY_ARMED_BENCH
#define AP_SPEKTRUM_SMART_ESC_EARLY_ARMED_BENCH 0
#endif
#if AP_SPEKTRUM_SMART_ESC_EARLY_ARMED_BENCH && !AP_SPEKTRUM_SMART_ESC_ARMED_BENCH_EXPERIMENTAL
#error "EarlyArmedBench requires explicit non-flight ArmedBench packet limits"
#endif
#if AP_SPEKTRUM_SMART_ESC_EARLY_ARMED_BENCH && AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT
#error "EarlyArmedBench cannot inherit the unarmable EarlyNative image"
#endif
#define AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_LINK (AP_SPEKTRUM_SMART_ESC_NATIVE_EARLY_BOOT || AP_SPEKTRUM_SMART_ESC_EARLY_ARMED_BENCH)
