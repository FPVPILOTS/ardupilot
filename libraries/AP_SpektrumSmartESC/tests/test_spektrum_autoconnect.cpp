/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../AP_SpektrumSmartESC_AutoConnect.h"
#include <cassert>
#include <cstdio>

using Auto = AP_SpektrumSmartESC_AutoConnect;
using State = Auto::State;
using Action = Auto::Action;
using Reason = Auto::Reason;

static void diagnostic_names_are_human_readable()
{
    assert(Auto::state_name(State::LISTEN)[0] == 'W');
    assert(Auto::state_name(State::LINKED)[0] == 'S');
    assert(Auto::reason_name(Reason::MASTER_CONFLICT)[0] == 'S');
    assert(Auto::reason_name(Reason::PEER_TIMEOUT)[0] == 'E');
}

static void disabled_never_transmits()
{
    Auto p;
    assert(p.update(999999, true, true) == Action::NONE);
    assert(p.status().state == State::OFF);
}

static void startup_fc_first_esc_first_simultaneous()
{
    // Link policy cannot infer the ESC's power state from an idle-high pin:
    // all three real power orders must work after a validated peer reply.
    for (int order = 0; order < 3; order++) {
        Auto p;
        p.enable(100);
        assert(p.update(1099, true, true) == Action::NONE);
        // In a 4S/6S bench, a line can be high even when the ESC is off:
        // never treat HIGH alone as proof that the peer is ready.
        if (order == 0) { // FC first: ESC power arrives during the 7.5 s window.
            assert(p.update(1100, true, false) == Action::NONE);
            assert(p.update(1101, true, true) == Action::BEGIN_HANDSHAKE);
            assert(p.handshake_complete(3500, 0x40, true, true));
        } else if (order == 1) { // ESC first: bus initially busy with broadcasts.
            assert(p.update(1100, true, false) == Action::NONE);
            assert(p.update(1500, true, true) == Action::BEGIN_HANDSHAKE);
            assert(p.handshake_complete(1510, 0x40, true, true));
        } else { // Simultaneous power: quiet startup followed by valid reply.
            assert(p.update(1100, true, true) == Action::BEGIN_HANDSHAKE);
            assert(p.handshake_complete(1115, 0x40, true, true));
        }
        assert(p.status().state == State::LINKED);
        assert(p.status().attempts == 1);
        assert(p.status().successful == 1);
        // Each route has a new peer timestamp; no stale RC state is restored.
        const uint32_t fresh = order == 0 ? 3510U : order == 1 ? 1520U : 1125U;
        assert(p.peer_seen(fresh, 0x40, true));
    }
}

static void requires_exact_peer_and_uart_restoration()
{
    Auto p; p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(!p.handshake_complete(1100, 0x41, true, true));
    assert(p.status().reason == Reason::BAD_PEER);
    assert(p.status().state == State::BACKOFF);
    assert(p.update(2100, true, true) == Action::NONE);
    assert(p.update(3100, true, true) == Action::BEGIN_HANDSHAKE);
    assert(!p.handshake_complete(3110, 0x40, true, false));
    assert(p.status().state == State::BACKOFF);
    assert(p.status().successful == 0);
}

static void peer_timeout_and_recovery_clears_link()
{
    Auto p; p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.handshake_complete(1010, 0x40, true, true));
    assert(!p.peer_seen(1020, 0x41, true));
    assert(!p.peer_seen(1020, 0x40, false));
    assert(p.peer_seen(1020, 0x40, true));
    assert(p.update(1319, true, true) == Action::NONE);
    assert(p.update(1320, true, true) == Action::NONE);
    assert(p.status().state == State::BACKOFF);
    assert(p.status().lost == 1);
    assert(!p.status().device_id_matched);
    assert(p.update(2320, true, true) == Action::NONE);
    assert(p.update(3320, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.handshake_complete(3333, 0x40, true, true));
    assert(p.status().successful == 2);
}

static void unarmable_zero_reply_lease_does_not_authenticate_peer()
{
    Auto p; p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.handshake_complete(1010, 0x40, true, true));
    assert(p.peer_seen(1020, 0x40, true));
    assert(p.update(1500, true, true, true) == Action::NONE);
    assert(p.status().state == State::LINKED);
    // The sensor reply only defers a *policy timeout*. It never changes
    // the recorded CRC-validated 0x21 peer timestamp or establishes identity.
    assert(p.status().last_peer_ms == 1020);
    assert(p.status().device_id_matched);
    assert(p.update(1501, true, true, false) == Action::NONE);
    assert(p.status().state == State::BACKOFF);
    assert(p.status().reason == Reason::PEER_TIMEOUT);
    assert(p.status().last_peer_ms == 1020);
    assert(p.status().lost == 1);
    // Safety, faults and competing masters still latch even when hold=true.
    Auto unsafe; unsafe.enable(0);
    assert(unsafe.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(unsafe.handshake_complete(1010, 0x40, true, true));
    assert(unsafe.update(1500, false, true, true) == Action::NONE);
    assert(unsafe.status().state == State::FAULT);
    Auto conflict; conflict.enable(0);
    assert(conflict.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(conflict.handshake_complete(1010, 0x40, true, true));
    conflict.competing_master();
    assert(conflict.update(1500, true, true, true) == Action::NONE);
    assert(conflict.status().state == State::YIELDED);
}

static void backoff_is_bounded()
{
    Auto p; p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    p.handshake_failed(1010);
    assert(p.update(2009, true, true) == Action::NONE);
    assert(p.update(2010, true, true) == Action::NONE);
    assert(p.update(3010, true, true) == Action::BEGIN_HANDSHAKE);
    p.handshake_failed(3020);
    assert(p.update(5020, true, true) == Action::NONE);
    assert(p.update(6020, true, true) == Action::BEGIN_HANDSHAKE);
    p.handshake_failed(6030);
    assert(p.update(10030, true, true) == Action::NONE);
    assert(p.update(16030, true, true) == Action::NONE);
    assert(p.update(17030, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.status().attempts == 4);
    assert(p.status().successful == 0);
}

static void late_heartbeat_cannot_revive_stale_link()
{
    Auto p; p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.handshake_complete(1020, 0x40, true, true));
    assert(!p.peer_seen(1320, 0x40, true));
    assert(p.status().state == State::BACKOFF);
    assert(p.status().lost == 1);
}

static void never_compete_with_master()
{
    Auto p; p.enable(0);
    p.competing_master();
    assert(p.status().state == State::YIELDED);
    assert(p.status().reason == Reason::MASTER_CONFLICT);
    assert(p.update(500000, true, true) == Action::NONE);
    p.enable(600000);
    assert(p.update(601000, true, true) == Action::BEGIN_HANDSHAKE);
}

static void safety_loss_and_uart_fault_are_latched()
{
    Auto p; p.enable(0);
    assert(p.update(1000, false, true) == Action::NONE);
    assert(p.status().state == State::FAULT);
    assert(p.status().reason == Reason::NOT_SAFE);
    assert(p.update(100000, true, true) == Action::NONE);
    p.enable(0);
    assert(p.update(1000, true, true) == Action::BEGIN_HANDSHAKE);
    p.uart_fault();
    assert(p.status().state == State::FAULT);
    assert(p.update(5000, true, true) == Action::NONE);
    p.disable();
    assert(p.update(9000, true, true) == Action::NONE);
}

static void timeout_and_uint32_clock_wrap()
{
    Auto p; p.enable(0xFFFFFF00U);
    assert(p.update(744U, true, true) == Action::BEGIN_HANDSHAKE);
    assert(p.update(8244U, true, true) == Action::NONE);
    assert(p.status().reason == Reason::HANDSHAKE_TIMEOUT);
    assert(p.status().state == State::BACKOFF);
    assert(p.status().successful == 0);
}

int main()
{
    diagnostic_names_are_human_readable();
    disabled_never_transmits();
    startup_fc_first_esc_first_simultaneous();
    requires_exact_peer_and_uart_restoration();
    peer_timeout_and_recovery_clears_link();
    unarmable_zero_reply_lease_does_not_authenticate_peer();
    backoff_is_bounded();
    late_heartbeat_cannot_revive_stale_link();
    never_compete_with_master();
    safety_loss_and_uart_fault_are_latched();
    timeout_and_uint32_clock_wrap();
    std::puts("11 Phase 17 automatic startup/reconnect policy tests passed (simulation only)");
}
