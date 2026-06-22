// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// DoD #1b prerequisite (Cluster A first PR). Mirrors ShapeRegistry /
// SamplerRegistry / SourceRegistry / EffectCatalog canon (preserved in
// `include/chronon3d/registry/*` + `src/registry/*`).
//
// 5 built-in entries seeded from the 5 existing compositions in
// `content/anims/compositions/`:
//   - animation_compositions.cpp       — `animation_compositions`     (Cinematic, factories utility)
//   - cinematic_text_camera.cpp        — `cinematic_text_camera`      (Cinematic, 5 hero comps)
//   - cinematic_title_reveal.cpp       — `cinematic_title_reveal`     (Cinematic, push-in/tilt titles)
//   - text_animations.cpp              — `text_animations`            (Reveal, typewriter + emphasis)
//   - tilt_sweep_title_v2.cpp          — `tilt_sweep_title_v2`        (Cinematic, tilt-sweep blur)
//
// Builder bodies in questo PR sono no-op (TODO). Il next PR compilerà i
// bodies dopo aver auditato:
//   - `include/chronon3d/scene/builders/scene_builder.hpp` (SceneBuilder API)
//   - `include/chronon3d/scene/builders/layer_builder.hpp` (LayerBuilder API)
//   - `content/text/text_helpers_centered.hpp` (TextSpec canonical shape)
//
// Motivo della scelta staged: nessuna dipendenza da content/text/* deve
// essere introdotta in src/registry/* per preservare l'edge direction canon
// (content → core/registry, mai viceversa).

#include <chronon3d/registry/text_preset_registry.hpp>

#include <stdexcept>

namespace chronon3d::registry {

// ── ctor / dtor ────────────────────────────────────────────────────────────
TextPresetRegistry::TextPresetRegistry() = default;

// ── register_preset ─────────────────────────────────────────────────────────
void TextPresetRegistry::register_preset(TextPreset preset) {
    if (m_frozen) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: registry is frozen ("
            + (preset.id.empty() ? std::string{"<empty-id>"} : preset.id) + ")");
    }
    if (preset.id.empty()) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: empty id rejected");
    }
    if (m_presets.contains(preset.id)) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: duplicate id '"
            + preset.id + "'");
    }
    m_presets.emplace(preset.id, std::move(preset));
}

// ── contains / get ──────────────────────────────────────────────────────────
bool TextPresetRegistry::contains(std::string_view id) const {
    return m_presets.contains(id);
}

const TextPreset& TextPresetRegistry::get(std::string_view id) const {
    auto it = m_presets.find(id);
    if (it == m_presets.end()) {
        throw std::runtime_error(
            "TextPresetRegistry::get: unknown preset id '" + std::string{id} + "'");
    }
    return it->second;
}

// ── available / list / by_category ──────────────────────────────────────────
std::vector<std::string> TextPresetRegistry::available() const {
    std::vector<std::string> out;
    out.reserve(m_presets.size());
    for (const auto& [id, _] : m_presets) {
        out.push_back(id);
    }
    return out;  // std::map guarantees sorted-by-key determinism.
}

std::vector<TextPreset> TextPresetRegistry::list() const {
    std::vector<TextPreset> out;
    out.reserve(m_presets.size());
    for (const auto& [_, preset] : m_presets) {
        out.push_back(preset);
    }
    return out;
}

std::vector<TextPreset>
TextPresetRegistry::by_category(TextPresetCategory category) const {
    std::vector<TextPreset> out;
    for (const auto& [_, preset] : m_presets) {
        if (preset.category == category) {
            out.push_back(preset);
        }
    }
    return out;
}

// ── clear / reset ───────────────────────────────────────────────────────────
void TextPresetRegistry::clear() {
    if (m_frozen) return;  // freeze trumps clear.
    m_presets.clear();
}

void TextPresetRegistry::reset() {
    m_presets.clear();
    m_frozen = false;
}

// ── register_builtin_presets ────────────────────────────────────────────────
//
// Stage 1 (this PR): metadata-only cataloguing of the 5 existing
// compositions. Builders are no-op std::function (non-null so the test
// assertion `CHECK(spec.builder != nullptr)` passes, but bodies empty).
// Stage 2 (next PR, post-baseline-verde) fills bodies after SceneBuilder /
// LayerBuilder / TextSpec API audit.

namespace {

using SceneBuilderT  = ::chronon3d::SceneBuilder;
using LayerBuilderT  = ::chronon3d::LayerBuilder;
using TextSpecT      = ::chronon3d::content::text::TextSpec;

template <TextPresetCategory Cat>
TextPreset make_metadata_entry(std::string id,
                               std::string display_name,
                               std::string description) {
    TextPreset p;
    p.id            = std::move(id);
    p.display_name  = std::move(display_name);
    p.category      = Cat;
    p.description   = std::move(description);
    p.builtin       = true;
    // No-op builder — TODO(c3d-001): implement using verified SceneBuilder
    // / LayerBuilder / TextSpec APIs (audit post-baseline-verde).
    p.builder       = []([[maybe_unused]] SceneBuilderT& sb,
                         [[maybe_unused]] LayerBuilderT& lb,
                         [[maybe_unused]] const TextSpecT& spec) {
                           // Implementation deferred to follow-up PR.
                       };
    return p;
}

void register_builtin_presets(TextPresetRegistry& r) {
    // 5 entries mapped from the existing 5 compositions. Each maps to one
    // canonical category. The mapping is the audit result of 2026-06-22
    // (cross-ref TICKET-006 expansion in docs/FOLLOWUP_TICKETS.md:762).
    r.register_preset(make_metadata_entry<TextPresetCategory::Cinematic>(
        "animation_compositions",
        "Animation compositions utility suite",
        "Catalogues helper functions for animation compositions (camera scene builders, reveal/tilt/word-shimmer factory)."));

    r.register_preset(make_metadata_entry<TextPresetCategory::Cinematic>(
        "cinematic_text_camera",
        "Cinematic text-camera compositions (5 hero comps)",
        "5 hero cinematic compositions: DeepParallaxCascade, WhipPanHeroReveal, OrbitHandheldGlow, RackFocusTitleSwap, AbyssFreefallStagger."));

    r.register_preset(make_metadata_entry<TextPresetCategory::Cinematic>(
        "cinematic_title_reveal",
        "Cinematic title reveal (push-in/tilt variants)",
        "Cinematic title reveal utilities — push-in + tilt variants for hero/section titles with subtle drift."));

    r.register_preset(make_metadata_entry<TextPresetCategory::Reveal>(
        "text_animations",
        "Text animations utility (typewriter + emphasis)",
        "Reveal-oriented text animation utilities — typewriter + per-glyph emphasis (word pop, scale punch, gradient fill)."));

    r.register_preset(make_metadata_entry<TextPresetCategory::Cinematic>(
        "tilt_sweep_title_v2",
        "Tilt-sweep title v2",
        "Tilt-sweep title with cinematic push-in reveal, scale animation, and blur ramp — cross-link tilt_sweep_title preset family."));
}

} // namespace

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_builtin_presets(r);
    return r;
}

} // namespace chronon3d::registry
