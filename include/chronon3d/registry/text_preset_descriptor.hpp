// ==============================================================================
// include/chronon3d/registry/text_preset_descriptor.hpp
//
// TEXT-RES-01 — Single-registry TextPresetDescriptor.
//
// PUBLIC API — defines `TextPresetDescriptor { id, metadata, builder,
// animator_factory, fixture }` plus `PresetMetadata`, `AnimatorFactory`,
// `FixtureId`, `TextPresetBuilder`, and the `TextPresetCategory` enum.
// This is the SINGLE canonical descriptor type used by TextPresetRegistry
// AND by AnimatorResolver (Stage 5 — the resolver queries ONLY the registry).
//
// Anti-duplication-guardrail
//   docs/ANTI_DUPLICATION_RULES.md §registry/resolver: there is exactly
//   ONE descriptor type (`TextPresetDescriptor`) and ONE registry type
//   (`TextPresetRegistry`).  AnimatorResolver does NOT maintain a
//   per-id table — it queries the registry for the descriptor's
//   `animator_factory` closure and dispatches it.  This header defines
//   the descriptor surface so the registry TU, the resolver TU, and
//   any downstream Cluster B authoring facade share the SAME shape.
//
// Header ladder (canonical include policy):
//   text_preset_descriptor.hpp  ← canonical, public (this file)
//   text_preset_registry.hpp    ← registry container, public
//                                 (re-includes this header)
//   text_preset_resolver.hpp    ← Cluster B public free function
//                                 (`wire_preset_text_run_params`)
//   animator_resolver.hpp       ← query-only front door
//                                 (no per-id table)
//
// Cross-references:
//   - docs/ANTI_DUPLICATION_RULES.md
//   - docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md §Fase 10
//   - TICKET-A4 (single-registry refactor rationale).
// ==============================================================================

#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextAnimatorSpec,
                                                          // Vec3, GlyphSelectorSpec,
                                                          // TextSelectorUnit,
                                                          // TextSelectorShape,
                                                          // TextPropertyBlendMode,
                                                          // OpacityProperty, ...
                                                          // PositionProperty,
                                                          // ScaleProperty,
                                                          // BlurProperty,
                                                          // TrackingProperty.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d {
class SceneBuilder;   // forward decl — full type in
                      // chronon3d/scene/builders/scene_builder.hpp
class LayerBuilder;   // forward decl — full type in
                      // chronon3d/scene/builders/layer_builder.hpp
}

namespace chronon3d::registry {

// ── TextPresetCategory ──────────────────────────────────────────────────────
//
// Matches `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Fase 10's
// 4 canonical categories.  MOVED here from text_preset_registry.hpp
// (Phase 3.3 — TEXT-RES-01) so the enum + the descriptor it annotates
// live in the same header (the registry header now re-exports via
// `#include <chronon3d/registry/text_preset_descriptor.hpp>`).
enum class TextPresetCategory {
    Reveal,    // FadeIn, BlurIn, SlideUp, SlideDown, ScaleIn, ...
    Emphasis,  // WordPop, ScalePunch, ColorAccent, GradientFill, ...
    Cinematic, // WideTrackingTitle, SoftDollyTitle, ...
    Subtitle,  // MinimalWhite, YellowKeyword, ActiveWordPop, KaraokeFill, ...
};

// snake_case-lowercase ASCII ↔ enum (no Unicode).
[[nodiscard]] inline std::string_view
to_string_view(TextPresetCategory c) noexcept {
    switch (c) {
        case TextPresetCategory::Reveal:    return "reveal";
        case TextPresetCategory::Emphasis:  return "emphasis";
        case TextPresetCategory::Cinematic: return "cinematic";
        case TextPresetCategory::Subtitle:  return "subtitle";
    }
    return "unknown";
}

[[nodiscard]] inline std::string
to_string(TextPresetCategory c) {
    return std::string{to_string_view(c)};
}

// ── PresetMetadata ──────────────────────────────────────────────────────────
//
// Display-side data for an individual text preset: name, category, free-text
// description, builtin flag.  All fields are POD; no function pointers or
// engines live here.  `id` is duplicated at the descriptor top level for
// O(1) map key lookup (mirrors entries' map key); downstream consumers
// should refer to `TextPresetDescriptor::id` rather than `metadata.id`.
struct PresetMetadata {
    std::string id;             // snake_case ASCII, unique across the registry.
    std::string display_name;   // human-readable, no canon constraints.
    TextPresetCategory category{TextPresetCategory::Reveal};
    std::string description;    // one-line free-text description.
    bool builtin{false};        // true if seeded via register_builtin_presets().
};

// ── FixtureId ───────────────────────────────────────────────────────────────
//
// Canonical pointer to a Visual Regression Harness fixture path.  The
// fixture pins the preset's rendered output to a golden PNG so any
// future regress in the preset's wiring surfaces deterministically.
// Convention: fully-qualified path under `tests/visual/` (mirrors the
// prior `// golden-frame-link:` comment convention that lived on the
// pre-refactor generator code).
using FixtureId = std::string;

// ── TextPresetBuilder ───────────────────────────────────────────────────────
//
// `builder` is the engine-side factory — invoked by the call site to
// register a TextSpec onto a LayerBuilder using the preset's motion +
// paint recipe.  Non-null per the registry-internal invariant enforced
// at register time (test test_text_preset_registry.cpp Sub-case 3).
// Signature unchanged from the pre-refactor `TextPreset::Builder`
// typedef for full back-compat.
using TextPresetBuilder = std::function<
    void(::chronon3d::SceneBuilder&,
         ::chronon3d::LayerBuilder&,
         const ::chronon3d::TextSpec&)>;

// ── AnimatorFactory ─────────────────────────────────────────────────────────
//
// `animator_factory` is the resolver-side factory — invoked by the
// canonical AnimatorResolver (Stage 5) to produce a TextAnimatorSpec for
// the preset.  Returns std::nullopt for presets with no canonical
// motion path (e.g. `minimal_white`); returns a populated
// TextAnimatorSpec for the other 21.  The factory takes
// `const PresetMetadata&` so it has read access to display / category /
// fixture data, and may dispatch to per-id rendering based on
// `meta.id` if it needs to apply id-specific tweaks (e.g. when the spec
// is rich / carries live-paint signals — the golden-frame wiring
// invariant in TICKET-A4).
//
// Pure-function contract: same `(meta)` input → same `TextAnimatorSpec`
// output (deterministic; per
// docs/DETERMINISM_TICKETS.md §cluster-A).  The factory is the single  // drift-allow: stale-ref
// source of truth for "what the resolver returns for this preset" —
// previously this lived as hardcoded `else if` branches in
// AnimatorResolver::compose_for.
using AnimatorFactory = std::function<
    std::optional<::chronon3d::TextAnimatorSpec>(const PresetMetadata&)>;

// ── TextPresetDescriptor ────────────────────────────────────────────────────
//
// Canonical descriptor for a text preset.  Layout exactly matches the
// spec from TEXT-RES-01:
//
//   {
//     id:                 unique snake_case key (mirrors map index);
//     metadata:           PresetMetadata  (display + category + builtin);
//     builder:            TextPresetBuilder (engine-side factory);
//     animator_factory:   AnimatorFactory (resolver-side factory);
//     fixture:            FixtureId  (golden-frame fixture path);
//   }
//
// Single source of truth for "what defines a text preset".
// Anti-duplication-guardrail: there is exactly ONE descriptor type.
// ANTI_DUPLICATION_RULES.md §registry/resolver.
struct TextPresetDescriptor {
    std::string id;                       // O(1) lookup key — mirrors m_presets key.
    PresetMetadata metadata;              // display + category + builtin
    TextPresetBuilder builder{};          // engine-side factory (call site)
    AnimatorFactory animator_factory{};   // resolver-side factory (Stage 5)
    FixtureId fixture{};                  // golden-frame fixture path
                                          // (Visual Regression Harness)

    // ── Back-compat: nested `Builder` member-typedef ─────────────────────────
    // The PRE-TEXT-RES-01 `struct TextPreset` declared
    //     using Builder = std::function<...>;
    // AS a *nested* member typedef.  Post-refactor, the same typedef lives
    // at namespace scope as `TextPresetBuilder` (canonical, free-standing).
    // To preserve compile-time back-compat for any downstream consumer
    // that wrote `TextPreset::Builder func;`, resurface the same typedef
    // as a nested member here.  Identical signature, identical type
    // (pointing at the free-standing alias).  Costs nothing; keeps
    // ANTI_DUPLICATION_RULES.md §back-compat-aliases intact.
    using Builder = TextPresetBuilder;
};

} // namespace chronon3d::registry
