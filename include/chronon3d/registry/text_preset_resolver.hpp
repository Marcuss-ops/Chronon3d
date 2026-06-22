// ─── text_preset_resolver.hpp — Cluster B public API surface ──────────────
//
// Stage 5+ exposes the AnimatorResolver table via a single public free
// function so the test harness and downstream authoring facade can
// verify preset wiring deterministically (without instantiating a
// LayerBuilder / SceneBuilder / Materialiser).
//
// The function takes (preset_id, TextSpec) and returns a fully-populated
// `TextRunParams` whose:
//
//   - `text`       = the supplied `spec` (moved into the returned value);
//   - `animators`  = a vector with the resolved TextAnimatorSpec from
//                    AnimatorResolver::compose_for(preset_id) pushed onto
//                    `animators[0]`, EMPTY when the preset has no canonical
//                    motion (`minimal_white`) or when the preset_id is not
//                    present in the registered 22-preset catalog.
//
// Downstream inspection pattern (no LayerBuilder required):
//
//   auto params = chronon3d::registry::wire_preset_text_run_params(
//                      "fade_in", my_spec);
//   if (params.animators.empty()) {
//       // Preset has no canonical motion.  Caller routes via plain  
//       // `lb.text("fade_in_text", params.text)` (no resolver output).
//   } else {
//       // Preset wired a TextAnimatorSpec.  Caller routes via
//       // `lb.text_run("fade_in_text", params).commit()` so the wired
//       // entry lands on PendingTextRun BEFORE the layer-level motion
//       // canon mutates the layer.
//       REQUIRE(params.animators.size() == 1);
//       CHECK(params.animators[0].id == "presetc_fade_in");
//   }
//
// Anti-circular dep: this header DOES NOT include any
// `content/text/text_*.hpp`.  `builder_params.hpp` provides the full
// `TextSpec` / `TextRunParams` types from the registry's canonical
// type system; `wire_preset_text_run_params` is implemented once in
// `src/registry/text_preset_registry.cpp` next to the AnimatorResolver
// it delegates to.

#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextRunParams

#include <string_view>
#include <utility>      // std::move

namespace chronon3d::registry {

/// Build a TextRunParams populated with the AnimatorResolver composition
/// for the given preset.  The returned value's `animators` vector contains
/// EXACTLY ONE entry when the resolver can produce a TextAnimatorSpec
/// for `preset_id`, and ZERO entries when the preset has no canonical
/// motion (`minimal_white`) or when `preset_id` is not in the registered
/// catalog.  Callers detect the no-motion case via
/// `params.animators.empty()` rather than a separate std::optional wrapper
/// — keeps downstream authoring facades ergonomic (no double-unwrap).
///
/// Determinism: pure function over (preset_id, spec).  No LayerBuilder,
/// no SceneBuilder, no FontEngine materialiser required to introspect
/// the wiring.  This is the canonical verification entry point for the
/// Stage 5+ test harness and any downstream Cluster B authoring facade.
///
/// @note The caller-supplied `spec` is CONSUMED by value.  Inside the
/// function body it is moved into `out.text` so callers that wish to
///        preserve their copy must pass a copy (lvalue) or explicitly
///        `wire_preset_text_run_params(id, my_spec_copy)` rather than
///        `std::move(my_spec)`.  The take-by-value signature is
///        deliberate: it makes move semantics visible at the call site
///        and prevents accidental unsafe use of the source spec after
///        the call.  See Sub-case 31's spec-move semantics assertion
///        for the runtime contract.
///
/// @param preset_id  Id of the preset in the registered catalog ("fade_in",
///                   "cinematic_text_camera", …).  Unknown ids yield an
///                   `animators.empty() == true` return value (fail-safe).
/// @param spec       The TextSpec that will populate
///                   `params.text` (moved into the returned value).
/// @return TextRunParams with `text = spec` and `animators.size()` in
///         {0, 1} depending on the resolver's output.
[[nodiscard]] TextRunParams
wire_preset_text_run_params(std::string_view preset_id,
                            TextSpec spec) noexcept;

}  // namespace chronon3d::registry
