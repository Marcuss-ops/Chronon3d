// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// DoD #1b prerequisite (Cluster A).  Stage 1 (PR `41cda40c`) shipped
// metadata-only cataloguing of the 5 compositions.  Stage 2 (this PR)
// fills the no-op builders (TODO(c3d-001)) with real SceneBuilder /
// LayerBuilder / TextSpec calls after a header audit of:
//   - `include/chronon3d/scene/builders/scene_builder.hpp`
//   - `include/chronon3d/scene/builders/layer_builder.hpp`
//   - `include/chronon3d/scene/builders/builder_params.hpp` (TextSpec)
//
// 5 built-in entries seeded from the existing compositions in
// `content/anims/compositions/` + `content/text/`:
//   - animation_compositions        → Cinematic (utility suite, Reveal+Cinematic mix)
//   - cinematic_text_camera         → Cinematic (5 hero comps, depth-driven)
//   - cinematic_title_reveal        → Cinematic (push-in / tilt hero titles)
//   - text_animations               → Reveal    (typewriter + word-stagger emphasis)
//   - tilt_sweep_title_v2           → Cinematic (cinematic-push tilt-sweep blur)
//
// Anti-circular-dependency: this .cpp DOES NOT include any
// `content/text/text_*.hpp`.  The edge direction canon
// (content → core/registry, mai viceversa) is preserved.
// The header `include/chronon3d/registry/text_preset_registry.hpp` stays
// include-light (forward-decls only); full type definitions of
// SceneBuilder / LayerBuilder / TextSpec are pulled in here, in the .cpp,
// where the builder bodies actually use them.
//
// ── content::text::TextSpec ↔ chronon3d::TextSpec bridge ───────────────
// The header forward-declares `namespace content::text { struct TextSpec; }`
// to keep the public surface include-light.  The canonical TextSpec
// actually lives in `chronon3d::TextSpec` (from builder_params.hpp).  We
// bridge the two by aliasing in this TU only — the std::function
// signature in the header (`void(..., const content::text::TextSpec&)`)
// and the lambda parameter type in the .cpp resolve to the same
// underlying type once the alias is in scope.  External callers that
// instantiate `chronon3d::TextSpec` can pass it to the Builder unchanged.

#include <chronon3d/registry/text_preset_registry.hpp>

#include <chronon3d/scene/builders/scene_builder.hpp>   // full SceneBuilder
#include <chronon3d/scene/builders/layer_builder.hpp>    // full LayerBuilder
#include <chronon3d/scene/builders/builder_params.hpp>   // full TextSpec (canonical)

#include <stdexcept>

// ── content::text::TextSpec alias (private to this TU) ───────────────────
// Resolves the forward declaration in the registry header.  Without this
// alias the std::function signature `void(..., const content::text::TextSpec&)`
// would not match a lambda accepting `const chronon3d::TextSpec&`.
namespace chronon3d::content::text {
    using TextSpec = ::chronon3d::TextSpec;
}

// Drift guard: catch any future divergence between the forward-declared
// `content::text::TextSpec` and the canonical `chronon3d::TextSpec`
// (e.g. if someone promotes TextSpec to a different namespace without
// updating the bridge). A failure here means all std::function Builder
// signatures in the registry stop matching the lambdas.
static_assert(
    std::is_same_v<::chronon3d::content::text::TextSpec, ::chronon3d::TextSpec>,
    "content::text::TextSpec alias must resolve to canonical chronon3d::TextSpec");


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
// Stage 2 (this PR): filled bodies using the audited SceneBuilder /
// LayerBuilder / TextSpec API. Each builder:
//
//   1. Sets the user-provided `spec` as the text content via `lb.text(name, spec)`.
//   2. Chains motion presets that give the preset its identity
//      (depth_reveal / soft_pop / scale_drop / focus_in / fade_in / float_idle).
//
// All motions use only `(f32, Frame [, EasingCurve])` signatures so we
// avoid constructing `Glow`/`DropShadow` struct literals (which would
// require pulling additional headers into this TU).  When DoD #1 reaches
// the 20-preset bar, instances that need polish (glow / shadow / multiple
// visual effects) will live in their own composition files in `content/`
// and will be reachable through the same registry.

namespace {

using SceneBuilderT  = ::chronon3d::SceneBuilder;
using LayerBuilderT  = ::chronon3d::LayerBuilder;
using TextSpecT      = ::chronon3d::content::text::TextSpec;

// ────────────────────────────────────────────────────────────────────────
// 1. animation_compositions — Cinematic utility suite
//
//    Identity: depth-reveal push-in + soft-pop settle + subtle float_idle
//    for hero/dashboard utility that needs the cinematic feel without
//    camera-lock-in. Mirrors the depth_reveal + float_idle combinations
//    in `animation_compositions.cpp` DeepParallaxCascade / OrbitHandheldGlow.
// ────────────────────────────────────────────────────────────────────────
TextPreset animation_compositions_entry() {
    TextPreset p;
    p.id           = "animation_compositions";
    p.display_name = "Animation compositions utility suite";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "Catalogues helper functions for animation compositions "
                     "(reveal/tilt/word-shimmer factory). Cinematic depth-reveal "
                     "+ soft-pop + float_idle motion preset.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        lb.text("anim_comp_text", spec)
          .depth_reveal(280.0f, Frame{45})
          .soft_pop(Frame{30})
          .float_idle(8.0f, Frame{120});
    };
    return p;
}

// ────────────────────────────────────────────────────────────────────────
// 2. cinematic_text_camera — Cinematic (5 hero comps)
//
//    Identity: depth-reveal driven by camera (no float_idle — camera
//    drives the parallax).  Mirrors DeepParallaxCascade / WhipPanHeroReveal /
//    RackFocusTitleSwap.  scale_drop + soft_pop keep the entrance sharp.
// ────────────────────────────────────────────────────────────────────────
TextPreset cinematic_text_camera_entry() {
    TextPreset p;
    p.id           = "cinematic_text_camera";
    p.display_name = "Cinematic text-camera compositions (5 hero comps)";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "5 hero cinematic compositions (DeepParallaxCascade, "
                     "WhipPanHeroReveal, OrbitHandheldGlow, RackFocusTitleSwap, "
                     "AbyssFreefallStagger). Camera-driven depth reveal.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        lb.text("camera_text", spec)
          .depth_reveal(260.0f, Frame{50})
          .scale_drop(0.95f, Frame{30})
          .soft_pop(Frame{24});
    };
    return p;
}

// ────────────────────────────────────────────────────────────────────────
// 3. cinematic_title_reveal — Cinematic (push-in / tilt variants)
//
//    Identity: classic scale_drop push-in + soft_pop settle.  The
//    fastest clean entrance for hero section titles.  Mirrors the
//    scene-canonical `cinematic_title_reveal.cpp` family.
// ────────────────────────────────────────────────────────────────────────
TextPreset cinematic_title_reveal_entry() {
    TextPreset p;
    p.id           = "cinematic_title_reveal";
    p.display_name = "Cinematic title reveal (push-in/tilt variants)";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "Cinematic title reveal utilities — push-in + tilt "
                     "variants for hero/section titles with subtle drift.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        lb.text("title_reveal_text", spec)
          .scale_drop(0.92f, Frame{40})
          .soft_pop(Frame{30});
    };
    return p;
}

// ────────────────────────────────────────────────────────────────────────
// 4. text_animations — Reveal (typewriter + emphasis)
//
//    Identity: fade_in + scale_drop.  Normalises to the typewriter /
//    word-stagger / per-glyph emphasis family.  Mirrors the Re-veal
//    substack of `text_animations.cpp`.
// ────────────────────────────────────────────────────────────────────────
TextPreset text_animations_entry() {
    TextPreset p;
    p.id           = "text_animations";
    p.display_name = "Text animations utility (typewriter + emphasis)";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Reveal-oriented text animation utilities — typewriter "
                     "+ per-glyph emphasis (word pop, scale punch, gradient fill). "
                     "fade_in + scale_drop entrance.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // fade_in / scale_drop both default to OutCubic, so we omit the
        // redundant explicit arg here. If a future preset wants a non-default
        // easing (e.g. bounce or elastic), pass it explicitly.
        lb.text("reveal_text", spec)
          .fade_in(Frame{20})
          .scale_drop(0.95f, Frame{30});
    };
    return p;
}

// ────────────────────────────────────────────────────────────────────────
// 5. tilt_sweep_title_v2 — Cinematic (tilt-sweep blur)
//
//    Identity: scale_drop push-in + focus_in blur ramp (the camera-blur-style
//    reveal).  Mirrors `tilt_sweep_title_v2.cpp` cinematic push-in family.
// ────────────────────────────────────────────────────────────────────────
TextPreset tilt_sweep_title_v2_entry() {
    TextPreset p;
    p.id           = "tilt_sweep_title_v2";
    p.display_name = "Tilt-sweep title v2";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "Tilt-sweep title with cinematic push-in reveal, "
                     "scale animation, and blur ramp — cross-link "
                     "tilt_sweep_title preset family.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        lb.text("tilt_sweep_text", spec)
          .scale_drop(1.08f, Frame{45})
          .focus_in(2.5f, Frame{30})
          .soft_pop(Frame{24});
    };
    return p;
}

void register_builtin_presets(TextPresetRegistry& r) {
    r.register_preset(animation_compositions_entry());
    r.register_preset(cinematic_text_camera_entry());
    r.register_preset(cinematic_title_reveal_entry());
    r.register_preset(text_animations_entry());
    r.register_preset(tilt_sweep_title_v2_entry());
}

} // namespace

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_builtin_presets(r);
    return r;
}

} // namespace chronon3d::registry
