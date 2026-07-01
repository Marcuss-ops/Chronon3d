// ── tests/support/text_run_builder_inspection.hpp
//
// FASE 5 reviewer-fixup (TICKET-109) — test-only header that mirrors
// the established `LayerBuilderInspector` pattern (see
// `tests/support/layer_builder_inspection.hpp` for the canonical shape).
// The 47+ `t.mutable_pending().X` callsites in
// `tests/authoring/test_animator_dsl.cpp` previously reached into the
// `Text` handle's underlying `PendingTextRun` through the public
// `mutable_pending()` accessor (PR 3 surface).  This inspector funnels
// that read access through a typed snapshot so:
//
//   1. The `mutable_pending()` public method on `Text` can be safely
//      removed in a future post-baseline-verde PR (TICKET-110) without
//      breaking the test surface — the inspector is the canonical
//      consumer of the underlying `PendingTextRun` from the test side.
//
//   2. Tests no longer rely on the `mutable_pending()` accessor being
//      present on the public `Text` class — they go through the
//      inspector pattern instead, which is the established convention
//      (LayerBuilderInspector for `LayerBuilder::m_text_runs`).
//
//   3. The snapshot carries a value-typed `name` field for the most
//      common test pattern (`CHECK(snap.name == "text_0")`) and a
//      non-owning `const PendingTextRun*` pointer + `operator->()`
//      forwarding for the heavier `params.text.X` access chain.
//
// Freeze compliance (AGENTS.md v0.1): this header lives at
// `tests/support/` and is NOT installed.  No public API surface in
// `include/chronon3d/` is touched.  The `Text` class's public
// `mutable_pending()` accessor is preserved (no removal in this PR)
// for backwards compatibility with the prior PR 3 surface; TICKET-110
// (deferred to post-baseline-verde) will remove the public accessor
// in lock-step with a future `friend class` declaration on `Text`.
//
// Usage (test TU):
//
//     #include "support/text_run_builder_inspection.hpp"
//     using chronon3d::authoring::testing::TextRunBuilderInspector;
//
//     Text t = layer.text("HELLO");
//     const auto snap = TextRunBuilderInspector::pending_of(t);
//     CHECK(snap.name == "text_0");                  // value-typed name.
//     CHECK(snap->params.text.font.font_path == ...);  // operator-> for chain.
//

#pragma once

#include <string>
#include <utility>

#include <chronon3d/authoring/text.hpp>            // Text (mutable_pending → PendingTextRun&)
#include <chronon3d/scene/builders/text_run_builder.hpp>  // PendingTextRun — explicit include (reviewer finding #7; do NOT rely on transitive pull from text.hpp)

namespace chronon3d {
namespace authoring {
namespace testing {

    // ── Value-typed snapshot of one Text handle's underlying PendingTextRun ──
    //
    // Lifetime-independent of any Text handle: tests store the snapshot
    // by value and the `name` field is a value-typed `std::string` copy.
    // The `pending` pointer is non-owning — it points back into the
    // parent LayerBuilder's `m_text_runs` storage; tests MUST NOT outlive
    // the underlying LayerBuilder.
    //
    // The field set is deliberately minimal:
    //   * `name`    — value-typed copy of the entry's name (most common
    //                 test pattern; avoids `.operator->()->name`).
    //   * `pending` — non-owning pointer to the underlying PendingTextRun
    //                 for `params.X` access via `operator->()`.
    //
    // If a downstream test needs additional fields, extend this struct
    // behind a new test-only accessor — DO NOT re-introduce a public
    // surface on `Text` for that purpose.
    struct TextRunSnapshot {
        std::string name;
        const chronon3d::PendingTextRun* pending;

        // Forward to the underlying PendingTextRun for ergonomic field
        // access.  Tests that need `snap.params.text.font.font_path`
        // write `snap->params.text.font.font_path` (operator->) or
        // `(*snap).params.text.font.font_path` (operator*).  This
        // preserves the existing test-callsite chain without forcing a
        // deep copy of the (potentially heavy) `TextRunParams` payload.
        const chronon3d::PendingTextRun* operator->() const noexcept {
            return pending;
        }
        const chronon3d::PendingTextRun& operator*()  const noexcept {
            return *pending;
        }
    };

    // ── Test-only inspector (static-free-function class) ─────────────────────
    //
    // Mirrors `LayerBuilderInspector::pending_runs(lb)` (test-only
    // surface for `LayerBuilder::m_text_runs`).  The 1:1 mirror is
    // `TextRunBuilderInspector::pending_of(t)` — each `Text` handle
    // corresponds to exactly ONE `PendingTextRun` entry, so the
    // return type is a single `TextRunSnapshot` (not a vector of
    // snapshots like the LayerBuilder variant).
    //
    // The `pending_of` static method reads through the public
    // `Text::pending()` const accessor (a value-typed `PendingTextRun&`
    // returned by the Text handle's internal pointer).  This is the
    // last public escape hatch we need: TICKET-110 (deferred) will
    // remove `Text::mutable_pending()` AND add a `friend class
    // TextRunBuilderInspector` declaration on `Text` so the inspector
    // survives the API surface reduction.
    class TextRunBuilderInspector {
    public:
        // Read a value-typed snapshot of the PendingTextRun underlying
        // the `Text` handle `t`.  The snapshot is independent of any
        // reallocation inside the parent LayerBuilder's `m_text_runs`
        // vector (each `PendingTextRun` is owned by a `unique_ptr`, so
        // push_back does not invalidate the pointed-to entry).
        //
        // Order: trivially 1:1 (each `Text` ↔ one `PendingTextRun`).
        // The `name` field is a `std::string`-by-value copy of
        // `t.pending().name`; the `pending` field is a non-owning
        // pointer to the same `PendingTextRun` the handle holds.
        //
        // Throws: trivially — body is exception-free.  However, the
        // returned `TextRunSnapshot::pending` is a non-owning raw pointer;
        // dereferencing it AFTER the parent `LayerBuilder` is destroyed is
        // undefined behaviour (use-after-free).  Tests MUST NOT outlive
        // the underlying `LayerBuilder` instance.  The `noexcept`
        // annotation is removed (reviewer finding #4) to make the lifetime
        // contract explicit — the function does not throw, but a downstream
        // dereference CAN cause SIGSEGV.
        static TextRunSnapshot
        pending_of(const chronon3d::authoring::Text& t) {
            return TextRunSnapshot{
                std::string(t.pending().name),   // value-typed name copy.
                &t.pending()                    // non-owning pointer.
            };
        }
    };

} // namespace testing
} // namespace authoring
} // namespace chronon3d
