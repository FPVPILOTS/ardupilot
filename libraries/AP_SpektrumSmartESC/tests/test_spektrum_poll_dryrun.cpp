/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_PollDryRun.h"
#include <cassert>
#include <cstdio>
using Guard = AP_SpektrumSmartESC_PollDryRun;
using E = Guard::Evidence;
using R = Guard::Reason;

static E linked(uint32_t now_ms)
{
    E e {};
    e.now_ms = now_ms;
    e.last_peer_ms = now_ms;
    e.uart_errors = 1;  // Historical FE before successful handshake is OK.
    e.peer_id = 0x40;
    e.linked = true;
    e.disarmed = true;
    e.emergency_stop = true;
    e.rx_restored = true;
    e.bus_idle = true;
    return e;
}

static void refuses_before_link()
{
    Guard g;
    E e = linked(1000);
    e.linked = false;
    assert(!g.update(e));
    assert(g.status().reason == R::NO_LINK);
    assert(g.status().previews == 0);
}

static void requires_disarmed_emergency_stop()
{
    Guard g;
    E e = linked(1000);
    e.disarmed = false;
    assert(!g.update(e) && g.status().reason == R::BENCH_UNSAFE);
    e.now_ms = e.last_peer_ms = 2000;
    e.disarmed = true;
    e.emergency_stop = false;
    assert(!g.update(e) && g.status().reason == R::BENCH_UNSAFE);
    e.emergency_stop = true;
    assert(g.update(e) && g.status().previews == 1);
}

static void requires_expected_peer()
{
    Guard g;
    E e = linked(1000);
    e.peer_id = 0x41;
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 2000;
    assert(!g.update(e) && g.status().reason == R::WRONG_PEER);
    e.peer_id = 0x40;
    assert(g.update(e));
}

static void requires_native_rx_restoration()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 2000;
    e.rx_restored = false;
    assert(!g.update(e) && g.status().reason == R::RX_NOT_READY);
    e.rx_restored = true;
    assert(g.update(e));
}

static void requires_fresh_recent_esc_handshake()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.now_ms = 2000;
    e.last_peer_ms = 1800;
    assert(!g.update(e) && g.status().reason == R::PEER_STALE);
    e.last_peer_ms = 2001;
    assert(!g.update(e) && g.status().reason == R::PEER_STALE);
    e.last_peer_ms = 1999;
    assert(g.update(e));
}

static void requires_stable_link_and_idle_bus()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    assert(g.status().reason == R::SETTLING);
    e.now_ms = e.last_peer_ms = 1999;
    assert(!g.update(e) && g.status().reason == R::SETTLING);
    e.now_ms = e.last_peer_ms = 2000;
    e.bus_idle = false;
    assert(!g.update(e) && g.status().reason == R::BUS_NOT_IDLE);
    e.bus_idle = true;
    assert(g.update(e) && g.status().reason == R::READY);
}

static void one_preview_per_link_never_a_motor_permission()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 2000;
    assert(g.update(e));
    assert(g.status().last_preview_length == 14);
    assert(g.status().previews == 1);
    for (uint8_t i = 0; i < 20; i++) {
        e.now_ms += 50;
        e.last_peer_ms = e.now_ms;
        assert(!g.update(e));
    }
    assert(g.status().reason == R::ALREADY_PREVIEWED);
    assert(g.status().previews == 1);
}

static void uart_new_error_latches_until_link_loss()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 2000;
    e.uart_errors = 2;
    assert(!g.update(e) && g.status().reason == R::UART_CHANGED);
    e.uart_errors = 1;
    assert(!g.update(e) && g.status().reason == R::UART_CHANGED);
    e.linked = false;
    assert(!g.update(e));
    e = linked(3000);
    e.uart_errors = 2;
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 4000;
    assert(g.update(e));
}

static void conflicting_master_latches_until_link_loss()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.competing_master = true;
    e.now_ms = e.last_peer_ms = 2000;
    assert(!g.update(e) && g.status().reason == R::COMPETING_MASTER);
    e.competing_master = false;
    assert(!g.update(e) && g.status().reason == R::COMPETING_MASTER);
}

static void link_loss_reenables_a_new_dryrun_not_control()
{
    Guard g;
    E e = linked(1000);
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 2000;
    assert(g.update(e));
    e.linked = false;
    assert(!g.update(e));
    assert(g.status().reason == R::NO_LINK);
    e = linked(3000);
    assert(!g.update(e));
    e.now_ms = e.last_peer_ms = 4000;
    assert(g.update(e));
    assert(g.status().link_sessions == 2);
    assert(g.status().previews == 2);
}

int main()
{
    refuses_before_link();
    requires_disarmed_emergency_stop();
    requires_expected_peer();
    requires_native_rx_restoration();
    requires_fresh_recent_esc_handshake();
    requires_stable_link_and_idle_bus();
    one_preview_per_link_never_a_motor_permission();
    uart_new_error_latches_until_link_loss();
    conflicting_master_latches_until_link_loss();
    link_loss_reenables_a_new_dryrun_not_control();
    std::puts("10 Phase 17E-C linked-bench dry-run safety tests passed (no TX)");
}
