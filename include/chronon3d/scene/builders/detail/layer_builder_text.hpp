// ── include/chronon3d/scene/builders/detail/layer_builder_text.hpp ────
//
// Phase-3.3 mechanical split.  This tiny header re-exposes the type
// surface that `LayerBuilder`'s text pipeline (`.text(...)` and
// `.text_run(...)`) commits against, without pulling in the full
// <chronon3d/scene/builders/text_run_builder.hpp> include chain.
//
// The split is structural, not behavioural:
//   * LayerBuilder's public class methods (`text(...)`, `text_run(...)`)
//     remain DECL-only in <chronon3d/scene/builders/layer_builder.hpp>
//     with their bodies living in src/scene/builders/layer_builder.cpp.
//   * Downstream TUs that pair with the text pipeline (consumer
//     modules, tests, tooling) can include ONLY this header for the
//     typed coupling surface — saving include latency where the
//     full text_run_builder.hpp chain is overkill.
//
// Anti-circular: full type definitions of `PendingTextRun`,
// `TextRunSpec`, `TextRunSpec`, and `TextRunBuilder` live in
// <chronon3d/scene/builders/text_run_builder.hpp>.  This header
// only forward-declares them.

#pragma once

#include <string>      // std::string
#include <string_view> // std::string_view

namespace chronon3d {

    // ── Pending text-run spec slot ───────────────────────────────────────────
    // Forward-declared here so the test-only inspector
    // (<chronon3d/scene/builders/test/layer_builder_inspection.hpp>)
    // can package a value-typed snapshot (`PendingRunSnapshot`)
    // without forcing every consumer TU to pull in the full type
    // chain.
    //
    // Full definition: <chronon3d/scene/builders/text_run_builder.hpp>.
    // The canonical fields consumed by the test inspector are:
    //   * std::string                              name;
    //   * TextRunSpec                            params;
    // See the inspector header for the snapshot view.
    struct PendingTextRun;

    // ── Text-run parameters ─────────────────────────────────────────────────
    // Forward-declared; the test-only inspector copies
    // `params.animators` (`std::vector<TextAnimatorSpec>`) into
    // `PendingRunSnapshot::animators` for stable post-build reads.
    //
    // Full definition: <chronon3d/scene/builders/text_run_builder.hpp>.
    struct TextRunSpec;

    // ── Text-run spec ───────────────────────────────────────────────────────
    // Forward-declared.  Distinct from TextRunSpec (which is the
    // build-time commit payload); TextRunSpec is the authoring
    // façade's ae-spec value type (TextRunBuilder currently emits
    // a TextRunSpec slot).
    //
    // Full definition: <chronon3d/text/text_run_spec.hpp>
    // (or <chronon3d/scene/builders/text_run_builder.hpp>).
    struct TextRunSpec;

    // ── Text-run builder (authoring façade) ─────────────────────────────────
    // Forward-declared.  The `.text_run(...)` method on LayerBuilder
    // returns a non-owning `TextRunBuilder&` reference into an internal
    // std::vector<std::unique_ptr<TextRunBuilder>> builder pool.
    //
    // Lifetime caveat: the reference is tied to the LayerBuilder
    // instance that produced it; the canonical authoring pattern
    // chains a single fluent expression and never holds the
    // reference across builder-mutation calls.
    //
    // Full definition: <chronon3d/scene/builders/text_run_builder.hpp>.
    class TextRunBuilder;

} // namespace chronon3d
