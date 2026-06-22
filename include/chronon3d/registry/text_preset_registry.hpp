// ─── Chronon3D — TextPresetRegistry public API (Cluster A, DoD #1b) ─────────
//
// Catalyst for DoD primo-milestone #1 ("20+ preset stabili Reveal/Emphasis/
// Cinematic/Subtitle") and unblocked by TICKET-006 (chronon3d_backend_text
// linkage, commit 235c1800).
//
// Mirrors established registry canon in `include/chronon3d/registry/`:
//   - ShapeRegistry:    std::map<std::string, Descriptor, std::less<>> +
//                       register / contains / get / available / list / clear
//   - SamplerRegistry:  same shape with descriptor of function-pointer +
//                       parameter struct
//   - SourceRegistry:   same shape with id-> descriptor
//   - EffectCatalog:    same shape + freeze() (preserved here)
//
// Categorical canon for kinetic-typography presets follows
// `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Fase 10 "Preset library
// produttiva" (Reveal/Emphasis/Cinematic/Subtitle).
//
// ## Tier del canon
// - Stringhe interne: snake_case ASCII, deterministic, no Unicode.
//
// ## Costruttori builder vs builder stampati
// - `TextPreset::Builder` = std::function<void(SceneBuilder&, LayerBuilder&,
//   const TextSpec&)>; le firme sono verificate al call site. I built-in
//   seedati in `src/registry/text_preset_registry.cpp` sono placeholder
//   no-op in questo PR (todo: riempire in PR successivo dopo audit API
//   di Scene::SceneBuilder / LayerBuilder / TextSpec).
//
// Vincoli anti-circular-dependency:
//   Forward-declared in this header: `SceneBuilder`, `LayerBuilder`.
//   Included: `<chronon3d/scene/builders/builder_params.hpp>`
//     (TICKET-012 — supplies the canonical `::chronon3d::TextSpec` that
//      `content::text::TextSpec` aliases in this header).
//   NOT included here: `scene_builder.hpp`, any `content/text/text_*.hpp`,
//     or any `chronon3d_backends_*` header.  Full SceneBuilder/LayerBuilder
//     definitions are pulled in only inside the .cpp consumer.

#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // canonical ::chronon3d::TextSpec — alias target.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>  // explicit — for the static_assert alias-drift guard (TICKET-012).
#include <vector>

namespace chronon3d {

class SceneBuilder;
class LayerBuilder;

namespace content::text {
// TICKET-012 — single source of truth: the public `content::text::TextSpec`
// is a thin alias for the canonical `::chronon3d::TextSpec` from
// builder_params.hpp.  External SDK consumers that instantiate
// `::chronon3d::TextSpec` can pass it directly to any registry Builder.
// The previous forward-decl (`struct TextSpec;`) collided with the
// `using` declaration in the registry TU — the alias must live HERE so
// the std::function signature in this header resolves to the canonical
// type for every consumer without a per-TU bridge.
using TextSpec = ::chronon3d::TextSpec;
// TICKET-012 — alias-drift guard.  If the canonical `::chronon3d::TextSpec`
// is ever renamed or promoted to a different namespace, the std::function
// signature in this header would silently mismatch downstream Builder
// lambdas.  This compile-time assertion catches such drift BEFORE the
// breakage reaches consumers (the canonicalization lives HERE — in this
// public header — so this is the ONLY place an alias-target refactor can
// touch the std::function signature).
static_assert(std::is_same_v<::chronon3d::content::text::TextSpec,
                             ::chronon3d::TextSpec>,
              "content::text::TextSpec alias must resolve to canonical chronon3d::TextSpec");
}

namespace registry {

// ── TextPresetCategory enum (matches Fase 10 canonical 4 categories) ────────
enum class TextPresetCategory {
    Reveal,       // FadeIn, BlurIn, SlideUp, etc. (auto-canonicalised in §Fase 10)
    Emphasis,     // WordPop, ScalePunch, ColorAccent, etc.
    Cinematic,    // WideTrackingTitle, SoftDollyTitle, GradientReveal, etc.
    Subtitle,     // MinimalWhite, YellowKeyword, ActiveWordPop, KaraokeFill, etc.
};

// snake_case-lowercase ASCII ↔ enum (no Unicode).
[[nodiscard]] inline std::string_view to_string_view(TextPresetCategory c) noexcept {
    switch (c) {
        case TextPresetCategory::Reveal:    return "reveal";
        case TextPresetCategory::Emphasis:  return "emphasis";
        case TextPresetCategory::Cinematic: return "cinematic";
        case TextPresetCategory::Subtitle:  return "subtitle";
    }
    return "unknown";
}

[[nodiscard]] inline std::string to_string(TextPresetCategory c) {
    return std::string{to_string_view(c)};
}

// ── TextPreset descriptor (POD) ─────────────────────────────────────────────
//
// `builder` è non-null per i 5 entry built-in (+ futuri); può essere nullo
// per utenti che registrano metadata-only. La non-nullità è verificata nei
// test (vedi `tests/test_text_preset_registry.cpp` Sub-case 3).
struct TextPreset {
    std::string id;             // snake_case ASCII, unique across the registry.
    std::string display_name;   // human-readable, no canon constraints.
    TextPresetCategory category{TextPresetCategory::Reveal};
    std::string description;    // one-line free-text description.
    bool builtin{false};        // true if seeded via register_builtin_presets().

    using Builder = std::function<
        void(SceneBuilder&, LayerBuilder&, const content::text::TextSpec&)>;
    Builder builder{};
};

// ── TextPresetRegistry (singleton-style per shape_registry/SamplerRegistry) ─
class TextPresetRegistry {
public:
    TextPresetRegistry();

    /// Register a preset. Throws std::runtime_error if the registry is frozen,
    /// if the id is empty, or if the id collides with an existing entry.
    void register_preset(TextPreset preset);

    /// Lock the registry — no further `register_preset` calls accepted.
    /// Pattern mirror: EffectCatalog::freeze() (src/registry/effect_catalog.cpp).
    void freeze() noexcept { m_frozen = true; }

    [[nodiscard]] bool is_frozen() const noexcept { return m_frozen; }

    /// O(log n) membership check.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Throws std::runtime_error if id is unknown.
    [[nodiscard]] const TextPreset& get(std::string_view id) const;

    /// Returns ids of all registered presets (sorted, deterministic).
    [[nodiscard]] std::vector<std::string> available() const;

    /// Returns all TextPreset entries (sorted by id).
    [[nodiscard]] std::vector<TextPreset> list() const;

    /// Returns all presets whose `category == category`.
    [[nodiscard]] std::vector<TextPreset>
    by_category(TextPresetCategory category) const;

    /// Erase all non-builtin entries; preserves builtin:false behaviour of
    /// `reset()`. Provided for test isolation.
    void clear();

    /// Erase all entries. Provided for test isolation.
    void reset();

private:
    std::map<std::string, TextPreset, std::less<>> m_presets;
    bool m_frozen{false};
};

/// Helper mirroring `make_default_shape_registry()` pattern — populates the
/// 22 built-in text presets (Reveal/Emphasis/Subtitle/Cinematic tiers,
/// per `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Fase 10).
///
/// Composition flow (the "compute" step the registry delegates to):
///   1. `register_builtin_presets(r)` seeds the `TextPreset` table.
///   2. Run-time consumers call `AnimatorResolver::compose_for(preset_id)`
///      (in `include/chronon3d/registry/animator_resolver.hpp`) for the
///      `TextAnimatorSpec` half of the wiring.
///
/// Returns the registry fully populated but UNFROZEN — callers may still
/// register additional presets before `freeze()`.  Production consumers
/// MUST call `freeze()` after construction (EffectCatalog parity,
/// enforced at `src/runtime/render_runtime.cpp` for the catalog mirror
/// and at PR-A4's static-singleton fixture in
/// `tests/text/test_text_preset_visual.cpp` for the canonical "default
/// text preset registry" surface today) so `register_preset` throws
/// `std::runtime_error` on any later mutation attempt.
TextPresetRegistry make_default_text_preset_registry();

} // namespace chronon3d::registry
} // namespace chronon3d
