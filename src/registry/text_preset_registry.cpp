// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// Cluster A DoD #1 (DoD primo-milestone #1 — "20+ preset stabili Reveal /
// Emphasis / Cinematic / Subtitle").  Stage 1 (PR `41cda40c`) shipped
// metadata-only cataloguing of the 5 compositionally-derived entries.
// Stage 2 (PR `ba107a7d`) filled the no-op builders (TODO(c3d-001))
// with real LayerBuilder API calls.  Stage 3 (this PR — Cluster A
// copy-modify PR — TODO(c3d-002)) ships the 17 addizionali ceiling,
// reaching 22 entries total (≥20+, DoD #1 verde).
//
// All 22 entries:
//   ─ REVEAL (9) ─────────────────────────────────────────────────────
//     1. text_animations            (PR 41cda40c, kept)
//     2. fade_in                    (Stage 3)
//     3. blur_in                    (Stage 3)
//     4. slide_up                   (Stage 3)
//     5. slide_down                 (Stage 3)
//     6. scale_in                   (Stage 3)
//     7. tracking_close             (Stage 3)
//     8. masked_line_reveal         (Stage 3)
//     9. word_cascade               (Stage 3)
//    10. character_cascade          (Stage 3)
//
//   ─ EMPHASIS (4) ───────────────────────────────────────────────────
//    11. word_pop                   (Stage 3)
//    12. scale_punch                (Stage 3)
//    13. color_accent               (Stage 3)
//    14. gradient_fill              (Stage 3)
//
//   ─ CINEMATIC (4) ──────────────────────────────────────────────────
//    15. animation_compositions     (PR 41cda40c, kept)
//    16. cinematic_text_camera      (PR 41cda40c, kept)
//    17. cinematic_title_reveal     (PR 41cda40c, kept)
//    18. tilt_sweep_title_v2        (PR 41cda40c, kept)
//
//   ─ SUBTITLE (4) ───────────────────────────────────────────────────
//    19. minimal_white              (Stage 3)
//    20. yellow_keyword             (Stage 3)
//    21. glow_pulse                 (Stage 3)
//    22. caption_box                (Stage 3)
//
// Anti-circular-dependency: this .cpp DOES NOT include any
// `content/text/text_*.hpp`.  Edge direction canon
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
//
// ── DoD #1 Catalyst → Visual Regression Harness ──────────────────────
// Each new factory function ships a `// golden-frame-link:` comment naming
// the Visual Regression Harness fixture that will validate the rendered
// PNG (`tests/visual_tests.cmake` + `tools/visual_quality_suite.py`).
// PR Stage 3 ships the registries + builder bodies + invocation tests
// (Sub-cases 11–27 in test_text_preset_registry.cpp).  Adding the
// PNG fixtures + golden-frame renders is the work of Fase 2 / Cluster E
// (Visual Regression Harness maturity); the registry commits first.

#include <chronon3d/registry/text_preset_registry.hpp>

#include <chronon3d/scene/builders/scene_builder.hpp>   // full SceneBuilder
#include <chronon3d/scene/builders/layer_builder.hpp>    // full LayerBuilder
#include <chronon3d/scene/builders/builder_params.hpp>   // full TextSpec (canonical)

#include <stdexcept>
#include <type_traits>  // explicit — for static_assert(std::is_same_v<...>) drift guard.

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
// Stage 3 (this PR — Cluster A copy-modify): 22 built-in entries.
// Each builder calls `lb.text("<name>", spec)` to register the user-provided
// TextSpec on the layer, then chains 1-3 motion presets that give the
// preset its identity.  All motion recipes use only `(f32, Frame [,
// EasingCurve])` signatures — no `Glow`/`DropShadow` struct literals.
// `[[maybe_unused]] SceneBuilderT& sb` is added to all lambdas for
// forward-compat with Cinematic builders that may use sb.camera() in
// later stages.

namespace {

using SceneBuilderT  = ::chronon3d::SceneBuilder;
using LayerBuilderT  = ::chronon3d::LayerBuilder;
using TextSpecT      = ::chronon3d::content::text::TextSpec;


// ── Cinematic factory functions (PR `41cda40c` — kept verbatim) ────────────

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
        // golden-frame-link: tests/visual/cinematic_motion/DeepParallaxCascade
        lb.text("anim_comp_text", spec)
          .depth_reveal(280.0f, Frame{45})
          .soft_pop(Frame{30})
          .float_idle(8.0f, Frame{120});
    };
    return p;
}

// ── Stage 4 (Cluster A DoD #2 - preset library + TextAnimator V2 integration)
// ─────────────────────────────────────────────────────────────────────────
// AnimatorResolver maps motion-preset canonal entries to TextAnimatorSpec
// entries that compose the per-glyph animated state alongside the layer
// level transform.  At Stage 4 we ship ONE conservative resolver mapping:
//
//     cinematic_text_camera  ─►  rich_paint_anchor("cinematic_text_camera")
//
// fired when the spec carries ANY of:
//   - appearance.paint.stroke_enabled        = true
//   - appearance.paint.fill_style.has_value() (a Fill variant populated)
//   - appearance.paint.stroke_style.has_value()
//
// These three signals proxy a "richly-painted text spec" intent (the
// caller-authored `paint.rich_text` style — gradient fill, outline, or
// stroke).  When resolved, the resolver pushes a global-selector
// TextAnimatorSpec with `id = "ctc_rich_<preset_id>"` onto
// params.animators BEFORE the cinematic_text_camera factory invokes the
// canonical motion-preset chain.  This satisfies the DoD #2 contract:
//
//   "preset registry resolves motion(id) against embedded animators
//    BEFORE invoking the motion-preset canon."
//
// Downstream stages can extend the resolver table for additional motion
// presets WITHOUT touching the cinematic_text_camera factory body —
// the factory is now: "spec_is_rich → wire rich_paint_anchor → chain
// motion canon".
struct AnimatorResolver {
    [[nodiscard]] static bool spec_is_rich(const TextSpecT& spec) noexcept {
        return spec.appearance.paint.stroke_enabled
            || spec.appearance.paint.fill_style.has_value()
            || spec.appearance.paint.stroke_style.has_value();
    }

    [[nodiscard]] static TextAnimatorSpec
    rich_paint_anchor(std::string_view preset_id) {
        TextAnimatorSpec a;
        a.id             = std::string{"ctc_rich_"} + std::string{preset_id};
        a.enabled        = true;
        a.transform_mode = TextPropertyBlendMode::Add;
        a.color_mode     = TextPropertyBlendMode::Replace;

        // Global glyph selector — every glyph receives weight=1 (the
        // After Effects-style "entire text as one unit" pattern).  The
        // id on the selector is the parent animator's id with a
        // `_sel_global` suffix so diagnostics can correlate the two.
        GlyphSelectorSpec sel;
        sel.id    = a.id + "_sel_global";
        sel.unit  = TextSelectorUnit::Glyph;
        sel.shape = TextSelectorShape::Square;
        sel.start  = {0.0f};
        sel.end    = {100.0f};
        sel.amount = {100.0f};
        a.selectors.push_back(sel);

        // No-op-at-render property: OpacityProperty{1.0f} keeps the
        // glyphs at the standard full opacity (1.0) so the resolver
        // entry is observable AND semantically full (it composes with
        // the canonical motion-preset canon via `evaluate_animator_stack`
        // rather than id-only).  Future Stage 5+ resolver extensions
        // can swap in PositionProperty{vec3} ramps tied to depth_reveal
        // / scale_drop without changing this contract.
        a.properties.push_back(OpacityProperty{1.0f});
        return a;
    }
};

TextPreset cinematic_text_camera_entry() {
    TextPreset p;
    p.id           = "cinematic_text_camera";
    p.display_name = "Cinematic text-camera compositions (5 hero comps)";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "5 hero cinematic compositions (DeepParallaxCascade, "
                     "WhipPanHeroReveal, OrbitHandheldGlow, RackFocusTitleSwap, "
                     "AbyssFreefallStagger). Camera-driven depth reveal. "
                     "Stage 4 (this PR - DoD #2): resolver-wires a "
                     "TextAnimatorSpec onto the TextRun when the spec "
                     "carries rich-paint signals (stroke_enabled / "
                     "fill_style / stroke_style).";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/camera/camera_visual_tests

        // ── Stage 4 wiring: resolve motion-id BEFORE chain ─────────────
        // The resolver fires first; when it does, we route through
        // `lb.text_run(...)` so the wired TextAnimatorSpec lands on
        // `params.animators` before the canonical motion-preset chain
        // mutates the layer.  When the spec is plain (default Title
        // Case text without a rich_paint signal), the resolver does
        // NOT fire and the original `lb.text(...)` path is unchanged
        // — Sub-cases 7, 8, 9 (which use `make_test_text_spec()` with
        // no rich signals) pass-through unchanged.
        if (AnimatorResolver::spec_is_rich(spec)) {
            TextRunParams params;
            params.text   = spec;
            params.animators.push_back(
                AnimatorResolver::rich_paint_anchor("cinematic_text_camera"));
            // .commit() returns LayerBuilder& so the canonical motion
            // presets still chain cleanly after the wiring step.
            lb.text_run("camera_text", params)
              .commit()
              .depth_reveal(260.0f, Frame{50})
              .scale_drop(0.95f, Frame{30})
              .soft_pop(Frame{24});
        } else {
            // Plain-spec path — identical to PR 41cda40c body so
            // Sub-cases 7-9 stay green without modification.
            lb.text("camera_text", spec)
              .depth_reveal(260.0f, Frame{50})
              .scale_drop(0.95f, Frame{30})
              .soft_pop(Frame{24});
        }
    };
    return p;
}

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
        // golden-frame-link: tests/visual/cinematic_motion/cinematic_title_reveal
        lb.text("title_reveal_text", spec)
          .scale_drop(0.92f, Frame{40})
          .soft_pop(Frame{30});
    };
    return p;
}

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
        // golden-frame-link: tests/visual/cinematic_motion/tilt_sweep_title_v2
        lb.text("tilt_sweep_text", spec)
          .scale_drop(1.08f, Frame{45})
          .focus_in(2.5f, Frame{30})
          .soft_pop(Frame{24});
    };
    return p;
}


// ── Reveal factory functions (Stage 3 — 9 addizionali) ─────────────────────

// 1. text_animations (Reveal) — kept from PR 41cda40c; the typewriter +
//    emphasis reveal preset.  Listed under Reveal because its dominant
//    identity is the typewriter fade-in entrance.
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
        // redundant explicit arg here.
        // golden-frame-link: tests/visual/PR3/pr3_compositions (text-heavy PR3 end-to-end)
        lb.text("reveal_text", spec)
          .fade_in(Frame{20})
          .scale_drop(0.95f, Frame{30});
    };
    return p;
}

// 2. fade_in — the canonical pixel-accurate reveal; no scale, no shift,
//    pure opacity ramp.  Use for body copy / subtitle-level text.
TextPreset fade_in_entry() {
    TextPreset p;
    p.id           = "fade_in";
    p.display_name = "FadeIn";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Pure opacity ramp over Frame{15}, no spatial motion. "
                     "Canonical reveal for body copy / subtitle-level text.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_fade_in
        lb.text("fade_in_text", spec)
          .fade_in(Frame{15})
          .soft_pop(Frame{10});
    };
    return p;
}

// 3. blur_in — focus_in (decreasing blur) + opacity ramp. The classic
//    info-graphic reveal pattern.
TextPreset blur_in_entry() {
    TextPreset p;
    p.id           = "blur_in";
    p.display_name = "BlurIn";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Focus ramp (4.0 → 0.0 blur) over Frame{30}, paired with "
                     "short fade_in.  Classic motion-graphic reveal pattern.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_blur_in
        lb.text("blur_in_text", spec)
          .focus_in(4.0f, Frame{30})
          .fade_in(Frame{15});
    };
    return p;
}

// 4. slide_up — fade_shift_vertical upward (offset.y > 0) + fade_in.
//    Nudge the text up from its resting position into place.
TextPreset slide_up_entry() {
    TextPreset p;
    p.id           = "slide_up";
    p.display_name = "SlideUp";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Vertical slide-up (from below, offset {0,200,0}) + "
                     "fade_in over Frame{25}.  Eyebrow / section-header pattern.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_slide_up
        lb.text("slide_up_text", spec)
          .fade_shift_vertical(Vec3{0.0f, 200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    return p;
}

// 5. slide_down — same as slide_up but offset {0,-200,0}.  Used for
//    bottom-docked subtitles that slide down from a screen edge.
TextPreset slide_down_entry() {
    TextPreset p;
    p.id           = "slide_down";
    p.display_name = "SlideDown";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Vertical slide-down (from above, offset {0,-200,0}) + "
                     "fade_in over Frame{25}.  Bottom-docked subtitle entry.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_slide_down
        lb.text("slide_down_text", spec)
          .fade_shift_vertical(Vec3{0.0f, -200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    return p;
}

// 6. scale_in — scale_drop from 0.85 → 1.0 + soft_pop. Section-header
//    crop variant.
TextPreset scale_in_entry() {
    TextPreset p;
    p.id           = "scale_in";
    p.display_name = "ScaleIn";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Scale drop (0.85 → 1.0) over Frame{25} + soft_pop "
                     "settle.  Section-header crop pattern.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_scale_in
        lb.text("scale_in_text", spec)
          .scale_drop(0.85f, Frame{25})
          .soft_pop(Frame{15});
    };
    return p;
}

// 7. tracking_close — tracking_breathing (subtle letter-spacing pulse)
//    over Frame{30}.  Continuous idle, no entrance motion.  Use after
//    the text has already entered via a parent cinematic preset.
TextPreset tracking_close_entry() {
    TextPreset p;
    p.id           = "tracking_close";
    p.display_name = "TrackingClose";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Letter-spacing idle pulse (tracking_breathing) over "
                     "Frame{30}.  Use AFTER a parent cinematic preset; "
                     "no entrance motion of its own.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_tracking_close
        lb.text("tracking_close_text", spec)
          .tracking_breathing(0.05f, Frame{30});
    };
    return p;
}

// 8. masked_line_reveal — center_split (mask-in from the middle out)
//    + horizontal fade_shift suggesting the lines reading in.
TextPreset masked_line_reveal_entry() {
    TextPreset p;
    p.id           = "masked_line_reveal";
    p.display_name = "MaskedLineReveal";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Center-split mask reveal (lines read out from the "
                     "middle) + horizontal fade_shift.  Mask-and-reveal pattern.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_masked_line
        lb.text("masked_line_text", spec)
          .center_split(Frame{30})
          .fade_shift_horizontal(Vec3{120.0f, 0.0f, 0.0f}, Frame{25});
    };
    return p;
}

// 9. word_cascade — word_stagger (inter-word stagger) + fade_in. Each
//    word enters in succession.
TextPreset word_cascade_entry() {
    TextPreset p;
    p.id           = "word_cascade";
    p.display_name = "WordCascade";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Per-word stagger (Frame{4} delay) + fade_in.  Each "
                     "word enters in succession — the classic kinetic-title "
                     "reveal pattern.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_word_cascade
        lb.text("word_cascade_text", spec)
          .word_stagger(Frame{4}, Frame{20})
          .fade_in(Frame{15});
    };
    return p;
}

// 10. character_cascade — same recipe as word_cascade but with Frame{2}
//     per-glyph delay.  Use when the input has clear glyph separators
//     (monospace, kerned display fonts).
TextPreset character_cascade_entry() {
    TextPreset p;
    p.id           = "character_cascade";
    p.display_name = "CharacterCascade";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Per-glyph stagger (Frame{2} delay) + fade_in.  Tighter "
                     "than WordCascade — best on monospace / kerned display.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_character_cascade
        lb.text("character_cascade_text", spec)
          .fade_in(Frame{15})
          .word_stagger(Frame{2}, Frame{20});
    };
    return p;
}


// ── Emphasis factory functions (Stage 3 — 4 addizionali) ────────────────────

// 11. word_pop — scale_drop up to 1.15 (overshoot) + soft_pop. The
//     bouncy per-word emphasis.
TextPreset word_pop_entry() {
    TextPreset p;
    p.id           = "word_pop";
    p.display_name = "WordPop";
    p.category     = TextPresetCategory::Emphasis;
    p.description  = "Bouncy scale overshoot (1.0 → 1.15) + soft_pop.  "
                     "Per-word emphasis pop — best in mid-flow body copy.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/emphasis_word_pop
        lb.text("word_pop_text", spec)
          .scale_drop(1.15f, Frame{8})
          .soft_pop(Frame{15});
    };
    return p;
}

// 12. scale_punch — scale_drop DOWN to 0.7 + soft_pop. The text
//     compresses then snaps back.
TextPreset scale_punch_entry() {
    TextPreset p;
    p.id           = "scale_punch";
    p.display_name = "ScalePunch";
    p.category     = TextPresetCategory::Emphasis;
    p.description  = "Compression scale_drop (1.0 → 0.7) + soft_pop "
                     "(snap-back).  Impact-punch emphasis.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/emphasis_scale_punch
        lb.text("scale_punch_text", spec)
          .scale_drop(0.70f, Frame{12})
          .soft_pop(Frame{20});
    };
    return p;
}

// 13. color_accent — pure fade_in; the colour itself lives in
//     `spec.appearance.color`, so the builder writes nothing about
//     colour identity.  Use when the CALLER customises the spec
//     appearance (e.g. accent colour set by the pipeline).
TextPreset color_accent_entry() {
    TextPreset p;
    p.id           = "color_accent";
    p.display_name = "ColorAccent";
    p.category     = TextPresetCategory::Emphasis;
    p.description  = "Pure fade_in; colour comes from spec.appearance.color "
                     "(set by the caller / pipeline).  Pipe colour-accent "
                     "without touching motion.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/emphasis_color_accent
        lb.text("color_accent_text", spec)
          .fade_in(Frame{12});
    };
    return p;
}

// 14. gradient_fill — downward slide + fade_in; pair with a
//     gradient spec.appearance.paint (caller-set) for the dashboard-look.
TextPreset gradient_fill_entry() {
    TextPreset p;
    p.id           = "gradient_fill";
    p.display_name = "GradientFill";
    p.category     = TextPresetCategory::Emphasis;
    p.description  = "Short downward slide + fade_in; gradient paint "
                     "comes from spec.appearance.paint (caller-set).";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/emphasis_gradient_fill
        lb.text("gradient_fill_text", spec)
          .fade_shift_vertical(Vec3{0.0f, 80.0f, 0.0f}, Frame{20})
          .fade_in(Frame{15});
    };
    return p;
}


// ── Subtitle factory functions (Stage 3 — 4 addizionali) ───────────────────

// 19. minimal_white — the no-motion subtitle baseline. The builder
//     only registers the spec on the layer; no entrance / no idle.
//     Use for static captions or burnt-in subs.
TextPreset minimal_white_entry() {
    TextPreset p;
    p.id           = "minimal_white";
    p.display_name = "MinimalWhite";
    p.category     = TextPresetCategory::Subtitle;
    p.description  = "No-motion baseline. Registers the spec on the layer "
                     "without any motion preset.  Use for static captions "
                     "or burnt-in subs.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/subtitle_minimal_white
        lb.text("minimal_white_text", spec);
    };
    return p;
}

// 20. yellow_keyword — per-word stagger + fade_in.  Designed to be
//     paired with a yellow appearance.color (caller-set) for
//     karaoke-style keyword highlighting.
TextPreset yellow_keyword_entry() {
    TextPreset p;
    p.id           = "yellow_keyword";
    p.display_name = "YellowKeyword";
    p.category     = TextPresetCategory::Subtitle;
    p.description  = "Per-word stagger (Frame{3}) + fade_in.  Pair with a "
                     "yellow appearance.color (caller-set) for karaoke-style "
                     "keyword highlighting.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/subtitle_yellow_keyword
        lb.text("yellow_keyword_text", spec)
          .word_stagger(Frame{3}, Frame{20})
          .fade_in(Frame{12});
    };
    return p;
}

// 21. glow_pulse — continuous tracking-breathing idle. The text breathes
//     subtly on screen.  Pair with a glow appearance.material (caller-set).
TextPreset glow_pulse_entry() {
    TextPreset p;
    p.id           = "glow_pulse";
    p.display_name = "GlowPulse";
    p.category     = TextPresetCategory::Subtitle;
    p.description  = "Continuous tracking-breathing idle (Frame{40}) — "
                     "subtle on-screen presence.  Pair with a glow "
                     "appearance.material (caller-set).";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/subtitle_glow_pulse
        lb.text("glow_pulse_text", spec)
          .tracking_breathing(0.08f, Frame{40});
    };
    return p;
}

// 22. caption_box — small downward motion (offset.y = 30) + fade_in.
//     Subtitle-like pixel-accurate dock-in.
TextPreset caption_box_entry() {
    TextPreset p;
    p.id           = "caption_box";
    p.display_name = "CaptionBox";
    p.category     = TextPresetCategory::Subtitle;
    p.description  = "Short downward dock (offset.y=30) + fade_in. "
                     "Pixel-accurate caption-box entry.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/subtitle_caption_box
        lb.text("caption_box_text", spec)
          .fade_shift_vertical(Vec3{0.0f, -30.0f, 0.0f}, Frame{18})
          .fade_in(Frame{12});
    };
    return p;
}


void register_builtin_presets(TextPresetRegistry& r) {
    // ── Cinematic (4) — PR `41cda40c` kept verbatim ──────────────────────
    r.register_preset(animation_compositions_entry());
    r.register_preset(cinematic_text_camera_entry());
    r.register_preset(cinematic_title_reveal_entry());
    r.register_preset(tilt_sweep_title_v2_entry());

    // ── Reveal (10) — 1 from PR 41cda40c + 9 from Stage 3 ───────────────
    r.register_preset(text_animations_entry());
    r.register_preset(fade_in_entry());
    r.register_preset(blur_in_entry());
    r.register_preset(slide_up_entry());
    r.register_preset(slide_down_entry());
    r.register_preset(scale_in_entry());
    r.register_preset(tracking_close_entry());
    r.register_preset(masked_line_reveal_entry());
    r.register_preset(word_cascade_entry());
    r.register_preset(character_cascade_entry());

    // ── Emphasis (4) — Stage 3 ───────────────────────────────────────────
    r.register_preset(word_pop_entry());
    r.register_preset(scale_punch_entry());
    r.register_preset(color_accent_entry());
    r.register_preset(gradient_fill_entry());

    // ── Subtitle (4) — Stage 3 ───────────────────────────────────────────
    r.register_preset(minimal_white_entry());
    r.register_preset(yellow_keyword_entry());
    r.register_preset(glow_pulse_entry());
    r.register_preset(caption_box_entry());
}

} // namespace

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_builtin_presets(r);
    return r;
}

} // namespace chronon3d::registry
