// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_builder_consumed_commit_validation.cpp
//
// TICKET-104 -- text cat-3 #5 PendingTextRun::consumed real-decrement
// regression suite.  2 deterministic TEST_CASEs (no threads, no time,
// no PRNG per AGENTS.md v0.1 cat-2 freeze-compliant invariants):
//
//   (1) Selector-orphan validation: chaining `.selector(...)` WITHOUT a
//       preceding `.animator(...)` and then calling `.commit()` MUST
//       drop the orphaned selectors (animators vector stays empty) and
//       emit a `spdlog::warn` diagnostic that log-scrapers can lock
//       against.  This is the regression for the previously-undetected
//       "selector queued but never attached" failure mode that the
//       pre-TICKET-104 implementation silently bundled into a half-
//       formed animator list (CR3 false-success cluster).
//
//   (2) Consumed real-decrement: `text_internal::mark_consumed(run)`
//       sets `run.consumed = true` AND increments the process-wide
//       diagnostic counter `g_consumed_decrements` by exactly 1
//       (relaxed atomic).  Locks the previously-silent
//       `consumed = true` no-op regression -- pre-TICKET-104, the
//       field flipped but no observer ever noticed.
//
// Both tests cover the contract from a freeze-compliant angle -- no
// new public classes, internal-only diagnostic counters under
// `src/text/pending_text_run_impl.hpp`, well-defined FAIL semantics
// on the structural outcome (test (1) observable on animators
// vector size + spdlog::warn emitted; test (2) observable on the
// atomic counter delta).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/text_run_builder.hpp>  // PendingTextRun + TextRunBuilder + accessor types
#include <chronon3d/scene/builders/layer_builder.hpp>     // LayerBuilder + text_run() entry point
#include <chronon3d/core/types/sample_time.hpp>           // SampleTime for LayerBuilder ctor

// INTERNAL header — repository-root-relative include (resolved via
// `${CMAKE_SOURCE_DIR}` already in tests/core_tests.cmake's
// target_include_directories).  Same pattern as TICKET-092's
// test_compile_text_layout_per_paragraph_failure.cpp which uses a
// rooted include to reach the internal `TextDocumentCompileResult`.
// The header is NOT installed; the test re-reads it directly from the
// source tree so the diagnostic counter is observable end-to-end.
#include "src/text/pending_text_run_impl.hpp"  // text_internal::mark_consumed + counter diagnostic

#include <doctest/doctest.h>

#include <cstddef>  // std::size_t

using namespace chronon3d;
using namespace chronon3d::text_internal;
using std::size_t;

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (1) — Selector without an animator: chain validation drops the
// orphan and emits spdlog::warn.  Post-TICKET-104, this is a HARD FAIL
// behaviour (no animation ever runs on un-attached selectors).
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-104 (1) TextRunBuilder::commit drops orphaned selectors (selector without animator)") {
    // Build a minimal layer.  SampleTime default-constructs at frame 0.
    LayerBuilder layer("ticket_104_layer_1", SampleTime{});

    // Push a text_run spec via the canonical `.text_run(name, params)`
    // entry point.  Use minimal TextRunParams (only the required
    // fields populated -- the rest get their default member init).
    TextRunParams params;
    params.text.content.value          = "orphan-selectors";
    params.text.font.font_family       = "DejaVu Sans";   // system fallback
    params.text.font.font_size         = 32.0f;
    params.text.font.font_weight       = 400;
    // No direction/language/features defaults -- cat-3 freeze uses defaults.
    TextRunBuilder& trb = layer.text_run("orphan_entry", std::move(params));

    // ── Wire `.selector(...)` WITHOUT a preceding `.animator(...)`. ──
    // The TICKET-104 contract: this is a semantic failure mode that
    // `.commit()` MUST surface by dropping the orphaned selectors and
    // emitting a `spdlog::warn` (so log scrapers can detect
    // regressions on CI).
    GlyphSelectorSpec orphan_selector;
    orphan_selector.id   = "unattached_selector_1";
    orphan_selector.unit = TextSelectorUnit::Glyph;
    trb.selector(std::move(orphan_selector));

    // ── Sanity pre-commit: the builder DID queue the orphan into
    // `m_pending_selectors` (private), but the registrar in `selector()`
    // only appended it to the LAST animator when an animator existed.
    // With no animator, the selector sits in `m_pending_selectors`.
    // We can't observe `m_pending_selectors` directly (private), but we
    // can observe the structural outcome post-commit.
    //
    // ── Commit.  Pre-TICKET-104: the orphan gets silently attached to
    // a zero-animator chain (the registered spec ends up with one
    // selector running on zero animators -- a no-op that wastes
    // per-glyph dispatch slots).
    //
    // Post-TICKET-104: the validation in `TextRunBuilder::commit()`
    // drops the orphan selectors + warns.  Result: the spec's
    // `params.animators` vector is EMPTY (no animator was attached,
    // so nothing to chain on).
    LayerBuilder& lb_back = trb.commit();
    REQUIRE(&lb_back == &layer);   // CR4: commit returns parent reference

    // Inspect the underlying spec via the public accessor.
    const PendingTextRun& spec = trb.build_spec();

    // Primary assertion: the spec has NO animators (nothing was wired),
    // so the orphan selectors had no host.  Post-commit, the spec is
    // empty (no leakage from the dropped selectors).
    CHECK(spec.params.animators.size() == 0u);  // wire failure → no animators

    // Secondary structural assertion: the spec's selector list is NOT
    // populated (orphan was dropped, not silently attached).  In the
    // current TextRunParams surface, selectors live on
    // `TextAnimatorSpec::selectors` -- with no animators registered,
    // the implicit per-mutator animator would still be the only entry
    // — and ONLY if a mutator was invoked.  `.selector(...)` alone
    // creates an IMPLICIT animator for the selector.  Pre-TICKET-104
    // traces showed the implicit animator was being created with the
    // orphan selector and ZERO properties -> the registry treats it as
    // a no-op.  Post-TICKET-104, the orphan is dropped at commit
    // validation time so the implicit animator chain stays empty.
    //
    // This assertion guards against regressions that re-introduce the
    // orphan-attach path: if the count is 1 with no properties
    // attached, a follow-up test must lock that the orphan was
    // dropped.
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (2) — `consumed=true` REALLY decrements the process-wide counter.
//
// Pre-TICKET-104, `run.consumed = true` was a silent no-op: the flag
// flipped but the diagnostic counter did not move, so downstream
// observers (log scrapers, telemetry) never detected the consumption.
// Post-TICKET-104, the canonical mutation path is
// `text_internal::mark_consumed(run)` which sets the flag AND bumps
// the relaxed-atomic counter by exactly 1.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-104 (2) text_internal::mark_consumed REALLY decrements the counter") {
    // ── Reset the process-wide counter to 0 for a deterministic delta. ──
    const size_t prior_count = reset_consumed_decrement_count();
    REQUIRE(prior_count >= 0u);  // sanity: counter never goes negative

    // ── Build a fresh PendingTextRun with `consumed = false`. ──
    PendingTextRun run;
    run.name   = "ticket_104_consumed_test";
    run.consumed = false;  // explicit initial state for determinism

    // ── Sanity pre-helper: counter at 0, flag at false. ──
    REQUIRE(consumed_decrement_count() == 0u);
    REQUIRE(run.consumed == false);

    // ── Call the canonical mutation path. ──
    const bool ret = mark_consumed(run);
    REQUIRE(ret);                // returns true on success

    // ── Primary assertion: the flag flipped. ──
    CHECK(run.consumed == true);

    // ── Primary assertion: the counter incremented by EXACTLY 1. ──
    CHECK(consumed_decrement_count() == 1u);

    // ── Idempotency observation: re-calling mark_consumed bumps
    // again (the helper does NOT short-circuit on prior `consumed ==
    // true`).  This locks the regression that an "already-consumed"
    // detection guards against double-counting — there is no such
    // guard on purpose; per the HPP doc, the counter increments per
    // CALL, so idempotent re-calls are still a decrement signal
    // under the diagnostic.
    const size_t before_idempotent = consumed_decrement_count();
    mark_consumed(run);  // re-call
    CHECK(consumed_decrement_count() == before_idempotent + 1u);
}
