// ─── src/registry/text_preset_internal_helpers.hpp ──────────────────────────
//
// M1.5#13 (1/4 — Subtitle/basic category extraction) — shared internal
// helpers extracted from the anon namespace in
// `src/registry/text_preset_registry.cpp` so per-category factory TUs
// (the 4 `text_preset_factories_*.cpp` files emitted across this ticket's
// 4-commit sequence) can call them without re-implementing the same
// preamble / wiring.
//
// ## Surface (file-local, internal-only)
//
//  (a) Type aliases — `LayerBuilderT` / `SceneBuilderT` / `TextSpecT`.
//
//  (b) `make_presetc_template(preset_id)` — TEXT-RES-01 scaffold builder
//      for `TextAnimatorSpec` (the `presetc_<id>` skeleton: enabled,
//      Add/Replace blend modes, default global Glyph selector covering
//      0→100 over the full duration).
//
//  (c) `wire_through_resolver(lb, preset_id, spec)` — engine-side factory
//      body helper that delegates to the Cluster B public API
//      (`::chronon3d::registry::wire_preset_text_run_params`) then
//      routes the returned `TextRunSpec` through
//      `lb.text_run(<name>, params).commit()`.  Single canonical pipeline
//      for all 22 built-ins.
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//   - This header is INTERNAL ONLY.  Not installed.  Not reachable from
//     `include/chronon3d/`.  Consumers live in `src/registry/` or sibling
//     internal TUs.
//   - No new registry / preset / sampler / catalog surface introduced
//     (ANTI_DUPLICATION_RULES.md §registry/resolver preserved — the
//     `builtin_text_preset_registry()` remains the single source of truth).
//   - No `content/text/text_*.hpp` includes — the canonical
//     `::chronon3d::TextSpec` is reached via `builder_params.hpp`.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
// canonical ::chronon3d::TextSpec, TextAnimatorSpec, GlyphSelectorSpec,
// TextSelectorUnit, TextSelectorShape, TextPropertyBlendMode,
// Position/Scale/Opacity/Blur/TrackingProperty, ...

#include <chronon3d/scene/builders/layer_builder.hpp>
// full ::chronon3d::LayerBuilder — required for the inline
// `wire_through_resolver` helper (its body calls
// `lb.text_run(<name>, params).commit()` which needs the complete type).

#include <chronon3d/registry/text_preset_descriptor.hpp>
// Provides PresetMetadata (consumed by compose helpers via the descriptor
// builder bodies in the factories).

#include <chronon3d/registry/text_preset_resolver.hpp>
// Public Cluster B free function `wire_preset_text_run_params(preset_id, spec)`
// reached by wire_through_resolver below.

#include <string>
#include <string_view>

namespace chronon3d::registry::internal {

// ── Type aliases (mirror legacy anon-namespace aliases verbatim) ──────────
//
// File-local alias names preserve byte-identical signature parity with the
// pre-M1.5#13 anon namespace.  Public consumers continue to see the
// canonical types from `builder_params.hpp`; the aliases here are
// ergonomic only — they do NOT introduce new public API.
using LayerBuilderT = ::chronon3d::LayerBuilder;
using SceneBuilderT = ::chronon3d::SceneBuilder;
using TextSpecT     = ::chronon3d::TextSpec;

// ── make_presetc_template — TEXT-RES-01 scaffold factory ────────────────────
//
// Returns the standard `presetc_<id>` TextAnimatorSpec skeleton:
//   - id             = "presetc_<id>"; enabled = true.
//     transform_mode = Add; color_mode = Replace.
//   - selectors      = [GlyphSelectorSpec{id = "<id>_sel_global",
//                                          unit=Glyph, shape=Square,
//                                          start=0, end=100, amount=100}].
//
// Each entry's `animator_factory` calls this at the start and then
// appends the entry-specific properties / selector overrides.  This is
// the FACTORED equivalent of the pre-TEXT-RES-01
// `AnimatorResolver::compose_for` preamble, kept in ONE place (here, in
// this internal helper header) so all 22 built-in entries share the
// canonical scaffold.  Verbatim port from the anon namespace of
// `src/registry/text_preset_registry.cpp` (commit pre-M1.5#13).
[[nodiscard]] inline TextAnimatorSpec
make_presetc_template(std::string_view preset_id) {
    TextAnimatorSpec a;
    a.id             = std::string{"presetc_"} + std::string{preset_id};
    a.enabled        = true;
    a.transform_mode = TextPropertyBlendMode::Add;
    a.color_mode     = TextPropertyBlendMode::Replace;

    GlyphSelectorSpec sel;
    sel.id     = a.id + "_sel_global";
    sel.unit   = TextSelectorUnit::Glyph;
    sel.shape  = TextSelectorShape::Square;
    sel.start  = {0.0f};
    sel.end    = {100.0f};
    sel.amount = {100.0f};
    a.selectors.push_back(sel);

    return a;
}

// ── wire_through_resolver — engine-side factory body helper ────────────────
//
// Thin factory-body helper that delegates to the Cluster B public API
// (`wire_preset_text_run_params`) and then routes the returned
// TextRunSpec through `lb.text_run(<entry_name>, params).commit()`.
// Single canonical pipeline: every text preset enters the render graph
// as a TextRunShape (ShapeType::TextRun), regardless of whether the
// resolver wired a TextAnimatorSpec or not.
//
// Verbatim port from the anon namespace of
// `src/registry/text_preset_registry.cpp` (commit pre-M1.5#13).
//
// Naming convention preserved: `<preset_id>_text` is the registered
// entry-shape name (matches pre-M1.5#13 behaviour exactly).
[[nodiscard]] inline LayerBuilderT&
wire_through_resolver(LayerBuilderT& lb,
                      std::string_view preset_id,
                      const TextSpecT& spec) {
    TextRunSpec params =
        ::chronon3d::registry::wire_preset_text_run_params(preset_id, spec);
    const std::string entry_name = std::string{preset_id} + "_text";
    return lb.text_run(entry_name, params).commit();
}

} // namespace chronon3d::registry::internal
