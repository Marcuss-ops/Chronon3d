// ── tests/support/text_run_builder_inspection.hpp
//
// FASE 5 reviewer-fixup (TICKET-109) + lock-step close-out (TICKET-110) —
// test-only header that mirrors the established `LayerBuilderInspector`
// pattern (see `tests/support/layer_builder_inspection.hpp` for the
// canonical shape).  The 47+ `t.mutable_pending().X` callsites in
// `tests/authoring/test_animator_dsl.cpp` previously reached into the
// `Text` handle's underlying `PendingTextRun` through the public
// `mutable_pending()` accessor (PR 3 surface).  This inspector funnels
// that read access through a typed snapshot so:
//
//   1. (TICKET-110) The `mutable_pending()` public method on `Text` was
//      demoted from public to private, and this inspector now reads
//      through the now-private accessor via a `friend class` grant
//      declared on `Text`.  Tests keep working without modification
//      beyond the signature change (`const Text&` → `Text&`); the
//      inspector is the canonical consumer of the underlying
//      `PendingTextRun` from the test side.
//
//   2. Tests no longer rely on the `mutable_pending()` accessor being
//      public on the `Text` class — they go through the inspector
//      pattern (LayerBuilderInspector for `LayerBuilder::m_text_runs`,
//      TextRunBuilderInspector for `Text::pending_`) which is the
//      established convention.
//
//   3. The snapshot carries a value-typed `name` field for the most
//      common test pattern (`CHECK(snap.name == "text_0")`) and a
//      non-owning `const PendingTextRun*` pointer + `operator->()`
//      forwarding for the heavier `params.text.X` access chain.
//
// Freeze compliance (AGENTS.md v0.1): this header lives at
// `tests/support/` and is NOT installed.  The TICKET-110 demotion of
// `Text::mutable_pending()` is contractive (removes public surface,
// adds a friend grant) and was executed under category 3 (legacy path
// removal) with explicit user authorization.
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
        // deep copy of the (potentially heavy) `TextRunSpec` payload.
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
    // TICKET-110: `pending_of` now takes `Text&` (non-const) and reaches
    // into the underlying `PendingTextRun` through the now-private
    // `Text::mutable_pending()` accessor via the `friend class
    // testing::TextRunBuilderInspector;` grant on `Text`.  The non-const
    // parameter is a deliberate breaking signature change — callers
    // must hold a non-const lvalue `Text` (which is the case in all 53
    // test callsites in `test_animator_dsl.cpp` because `Text` is a
    // move-only type that callers bind to a local named variable).
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
        // `t.mutable_pending().name`; the `pending` field is a non-owning
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
        //
        // TICKET-110: signature changed from `const Text&` to `Text&` so
        // the body can call the now-private `mutable_pending()` accessor
        // via the `friend class` grant on `Text`.  All 53 test callsites
        // pass non-const local `Text` lvalues, so the migration is
        // transparent.
        static TextRunSnapshot
        pending_of(chronon3d::authoring::Text& t) {
            return TextRunSnapshot{
                std::string(t.mutable_pending().name),  // value-typed name copy (via friend).
                &t.mutable_pending()                    // non-owning pointer (via friend).
            };
        }
    };

} // namespace testing
} // namespace authoring
} // namespace chronon3d
