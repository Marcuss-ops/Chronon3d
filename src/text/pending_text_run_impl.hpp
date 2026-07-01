// ═══════════════════════════════════════════════════════════════════════════
// src/text/pending_text_run_impl.hpp — INTERNAL helper (cat-3 freeze-aligned)
//
// TICKET-104 — text cat-3 #5 PendingTextRun::consumed real decrement.
//
// Provides the canonical mutation path for the `consumed` flag on
// `PendingTextRun`: `text_internal::mark_consumed(run)` flips the flag
// from false to true AND increments an atomic diagnostic counter so
// downstream tests / consumers can verify that consumption actually
// happened (the previous documentation claimed `consumed` was updated
// on commit, but the field was a no-op and the diagnostic was missing).
//
// Anti-duplication invariant: this is the ONLY path that should change
// the consumed flag.  Direct assignment (`run.consumed = true`) is
// supported for backwards compatibility (12+ sites) but is documented in
// `mutable_pending()` as the unsupported "before-TICKET-104" pattern;
// future tickets should migrate those sites to `mark_consumed`.
//
// Freeze compliance:
//   • Lives under `src/text/`, NOT in `include/chronon3d/` (cat-violator
//     if promoted — direct API-surface swap-out would re-expand the
//     public type catalogue and is reserved for post-baseline-verde).
//   • No new public classes — only one free function + two diagnostic
//     accessors inside an existing (text_internal) anonymous namespace
//     replica pattern, modelled on `src/text/text_document_compile_result.hpp`.
//   • No new `#include` in `include/chronon3d/`.
//
// Diagnostic counter (`consumed_decrement_count`) is INTENTIONALLY
// non-parallel with any external metric: it counts `mark_consumed()`
// CALLS, not (a) gate events, (b) layer-builder calls, (c) materializer
// invocations.  The count's purpose is to lock the regression
// `consumed=true actually fires`, not to drive allocation / perf.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/builders/text_run_builder.hpp>  // PendingTextRun

#include <cstddef>

namespace chronon3d {
namespace text_internal {

/// Mark `run.consumed = true` AND increment the process-wide
/// `g_consumed_decrements` atomic counter (relaxed memory order — the
/// counter is a diagnostic for tests, not a sync primitive).
///
/// This is the canonical mutation path for `PendingTextRun::consumed`.
/// Called from `TextRunBuilder::commit()` and from
/// `LayerBuilder::build()` (where `spec.consumed = true` was previously
/// an undocumented no-op).
[[nodiscard]] bool mark_consumed(PendingTextRun& run) noexcept;

/// Snapshot the current value of the diagnostic counter.
[[nodiscard]] std::size_t consumed_decrement_count() noexcept;

/// Atomically reset the diagnostic counter to 0 and return the prior
/// value.  Useful for TEST_CASE setup that wants a deterministic
/// delta (assert `after - before == 1`).
[[nodiscard]] std::size_t reset_consumed_decrement_count() noexcept;

} // namespace text_internal
} // namespace chronon3d
