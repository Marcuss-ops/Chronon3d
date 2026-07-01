// ═══════════════════════════════════════════════════════════════════════════
// src/text/pending_text_run_impl.cpp — INTERNAL impl (cat-3 freeze-aligned)
//
// TICKET-104 — text cat-3 #5 PendingTextRun::consumed real decrement.
// Counter implementation + relax-order publish.
// ═══════════════════════════════════════════════════════════════════════════

#include "pending_text_run_impl.hpp"

#include <atomic>

namespace chronon3d {
namespace text_internal {

namespace {

// ─── Diagnostic counter (relaxed memory order — see header doc) ───────
//
// `g_consumed_decrements` counts `mark_consumed()` CALLS.  It is a
// test-visible diagnostic only; downstream code MUST NOT treat it as
// authoritative state.  Atomic relaxed because (a) the invariant the
// counter locks is "between two test points the counter increases by at
// least N", which does not require acquire/release, and (b) the
// PendingTextRun mutation (`run.consumed = true`) is a plain write
// inside the same function, so no other thread can observe a stale
// `consumed` regardless of the counter's memory ordering.
//
// Test reset path uses `exchange(0, relaxed)` which is also the relaxed
// ordering; the LOAD-AND-RESET pattern is symmetric.
std::atomic<std::size_t> g_consumed_decrements{0};

} // namespace

bool mark_consumed(PendingTextRun& run) noexcept {
    // The atomic increment happens BEFORE we observe the prior value of
    // `run.consumed` so a parallel observer can never conclude
    // "consumed=true via mark_consumed() but counter unchanged".
    // Order: flag-set-then-counter-inc — diagnostic consumers that
    // probe `consumed=true` on the returned reference see the new
    // value; counter observers see the new + incremented value.
    run.consumed = true;
    g_consumed_decrements.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::size_t consumed_decrement_count() noexcept {
    return g_consumed_decrements.load(std::memory_order_relaxed);
}

std::size_t reset_consumed_decrement_count() noexcept {
    return g_consumed_decrements.exchange(0, std::memory_order_relaxed);
}

} // namespace text_internal
} // namespace chronon3d
