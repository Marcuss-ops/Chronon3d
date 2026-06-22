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
//   Questo header NON include `include/chronon3d/scene/builders/scene_builder.hpp`
//   né alcun `content/text/text_*.hpp` né `chronon3d_backends_*`.
//   Tali dipendenze sono forward-declared. Le full-includes avvengono solo
//   nel .cpp consumatore (per metadata seed) e nei call site finali.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

class SceneBuilder;
class LayerBuilder;

namespace content::text {
struct TextSpec;  // forward decl — TODO(c3d-001): promote to include/chronon3d/text/text_spec.hpp post-Fase 0 baseline.
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
/// 5 built-in entries seeded from the existing 5 compositions in
/// `content/anims/compositions/`. Returns the registry fully populated but
/// UNFROZEN — callers may still register additional presets before freeze().
TextPresetRegistry make_default_text_preset_registry();

} // namespace chronon3d::registry
} // namespace chronon3d
