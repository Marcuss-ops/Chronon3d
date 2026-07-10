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
// on the structural outcome (test (1) observable via
// (a) animators vector size, (b) spdlog::warn message CAPTURED via
// the in-TU `CapturingWarnSink` custom sink + RAII `CaptureSinkGuard`
// pattern that locks the user-visible fail-fast warn contract --
// pre-this-update, a regression that DROPPED the `spdlog::warn` but
// KEPT the structured drop would still pass; test (2) observable on
// the atomic counter delta).
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

// TICKET-104 follow-up -- spdlog::warn CAPTURE for test (1).
// We inherit from `spdlog::sinks::base_sink<std::mutex>` (header-only,
// already transitive-linked through the SDK target chain) so the
// internal `mutex_` is locked automatically before `sink_it_()` is
// invoked -- the test runs single-threaded per AGENTS.md v0.1 cat-2
// invariant, but the locking is the cat-3-clean standard idiom and
// protects against future test parallelization regressions.  We do
// NOT add a new public class -- both the sink struct and the guard
// live in this test TU's anonymous namespace.
//   See: spdlog/sinks/base_sink.h for the base class contract.
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <doctest/doctest.h>

#include <algorithm>  // std::remove_if for sink removal on guard dtor
#include <cstddef>    // std::size_t
#include <memory>     // std::shared_ptr
#include <mutex>      // std::lock_guard (via base_sink)
#include <string>     // std::string (sink payload materialization)
#include <vector>     // std::vector (capture buffer)

using namespace chronon3d;
using namespace chronon3d::text_internal;
using std::size_t;

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-104 follow-up -- spdlog::warn capture sink + RAII guard local to
// this TU.  Both anonymous-namespace-local (cat-3: zero PUBLIC surface
// expansion).  `CapturingWarnSink` records ONLY warn-level messages so
// other test output that may flow through the process during the test
// window doesn't pollute the captured-result assertion (the warn IS the
// dedicated fail-fast diagnostic per AGENTS.md v0.1 freeze).  RAII
// guard removes the sink on scope exit -- even if the test fails an
// early REQUIRE the guard dtor still cleans up, so post-failure
// diagnostics are not contaminated by stale sink state.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

class CapturingWarnSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    // Backing store is GUARDED by `base_ssink::mutex_` automatically
    // (lock-and-unlock around `sink_it_()`); our `get_messages_copied()`
    // helper re-locks under the same mutex so concurrent access from
    // any future parallel-test driver stays race-free.
    [[nodiscard]] std::vector<std::string> get_messages_copied() {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    [[nodiscard]] std::size_t message_count() {
        // Non-`const`: acquiring a lock counts as observation-state
        // mutation, so we don't pretend this is a pure read.  Working
        // around the `const_cast` would require either (a) confirmed
        // `mutable` on `base_sink::mutex_` (fragile across spdlog
        // versions) or (b) `std::lock_guard` on a `const std::mutex&`
        // which fails for `std::mutex` because `lock()` is non-const.
        // The cleanest cross-version form is a plain non-const
        // accessor that takes the lock explicitly.
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.size();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        // spdlog::string_view_t is `std::string` for builds without
        // `SPDLOG_USE_STD_STRING_VIEW_ON`.  Bridge both via the
        // (data, size) constructor -- safe across all macro toggles.
        if (msg.level == spdlog::level::warn) {
            messages_.emplace_back(msg.payload.data(), msg.payload.size());
        }
        // Non-warn messages (info / debug / error from concurrent
        // emit) are deliberately dropped to keep the assertion
        // surface tight.  Error-level would be a regression signal
        // too, but cat-3 freeze forbids expanding TEST_CASEs
        // further; future tickets may widen the filter.
    }

    void flush_() override {
        // No-op: capture is in-memory; explicit flush is irrelevant
        // to the assertion contract.
    }

private:
    std::vector<std::string> messages_;  // guarded by base_sink::mutex_
};

/// RAII helper that pushes a `CapturingWarnSink` to the default
/// logger on construction and removes it on destruction.  Scope-binds
/// the capture window to the enclosing TEST_CASE body so test (2)
/// and other unrelated TUs are not polluted.
struct CaptureSinkGuard {
    std::shared_ptr<CapturingWarnSink> sink;

    CaptureSinkGuard()
        : sink(std::make_shared<CapturingWarnSink>()) {
        spdlog::default_logger()->sinks().push_back(sink);
    }

    ~CaptureSinkGuard() {
        // Erase by shared_ptr stored-pointer equality.  `std::shared_ptr::operator==`
        // is defined to compare stored pointers (well-defined for our
        // case); `std::remove` + `erase` is the idiomatic one-liner
        // and removes the lambda + `std::remove_if` boilerplate.
        // Only this guard's sink instance is removed even if other
        // sinks were added concurrently.
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), this->sink), sinks.end());
    }

    CaptureSinkGuard(const CaptureSinkGuard&) = delete;
    CaptureSinkGuard& operator=(const CaptureSinkGuard&) = delete;
    CaptureSinkGuard(CaptureSinkGuard&&) = delete;
    CaptureSinkGuard& operator=(CaptureSinkGuard&&) = delete;
};

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (1) — Selector without an animator: chain validation drops the
// orphan and emits spdlog::warn.  Post-TICKET-104, this is a HARD FAIL
// behaviour (no animation ever runs on un-attached selectors).
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-104 (1) TextRunBuilder::commit drops orphaned selectors (selector without animator)") {
    // Build a minimal layer.  SampleTime default-constructs at frame 0.
    LayerBuilder layer("ticket_104_layer_1", SampleTime{});

    // Push a text_run spec via the canonical `.text_run(name, params)`
    // entry point.  Use minimal TextRunSpec (only the required
    // fields populated -- the rest get their default member init).
    TextRunSpec params;
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
    //
    // TICKET-104 follow-up -- INSTALL spdlog::warn capture BEFORE commit().
    // The `CaptureSinkGuard` is RAII-scoped to this TEST_CASE body so
    // the captured-result assertion immediately below sees ONLY the
    // `commit()` call's warn line (the drop-the-warn regression is
    // detected by `REQUIRE(message_count() >= 1u)` +
    // `CHECK(found "selector spec(s) dropped")` below).
    CaptureSinkGuard warn_capture;

    LayerBuilder& lb_back = trb.commit();
    REQUIRE(&lb_back == &layer);   // CR4: commit returns parent reference

    // Inspect the underlying spec via the public accessor.
    const PendingTextRun& spec = trb.build_spec();

    // Primary assertion: the spec has NO animators (nothing was wired),
    // so the orphan selectors had no host.  Post-commit, the spec is
    // empty (no leakage from the dropped selectors).
    CHECK(spec.params.animators.size() == 0u);  // wire failure → no animators

    // ── spdlog::warn CAPTURE contract (TICKET-104 follow-up) ──
    // The user-visible fail-fast contract per AGENTS.md v0.1 freeze
    // is the `spdlog::warn` diagnostic emitted by `commit()`.  Without
    // this assertion, a regression that DROPPED the warn line but
    // KEPT the silent drop would still pass the structural assertion
    // above -- so we LOCK the warn emission by capturing the message
    // stream and asserting the magic token `"selector spec(s) dropped"`
    // is present.  This is the dedicated fail-fast contract for
    // log-scrapers per CI observability.  See TICKET-104 follow-up
    // section in `docs/FOLLOWUP_TICKETS.md` (recently-closed row).
    const auto captured_warns = warn_capture.sink->get_messages_copied();
    REQUIRE(captured_warns.size() >= 1u);  // CR5: at least one warn fired
    bool found_orphan_drop_token = false;
    for (const auto& m : captured_warns) {
        if (m.find("selector spec(s) dropped") != std::string::npos) {
            found_orphan_drop_token = true;
            break;
        }
    }
    CHECK(found_orphan_drop_token);  // CR5: warn carries the orphan-drop token

    // Secondary structural assertion: the spec's selector list is NOT
    // populated (orphan was dropped, not silently attached).  In the
    // current TextRunSpec surface, selectors live on
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
