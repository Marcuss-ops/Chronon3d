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
// (TICKET-012) The `content::text::TextSpec` alias lives in
// `include/chronon3d/registry/text_preset_registry.hpp` (single source
// of truth).  No per-TU bridge is needed here; the .cpp consumers see
// identical `TextSpecT = ::chronon3d::content::text::TextSpec` types
// resolving to `::chronon3d::TextSpec` from `builder_params.hpp`.
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

#include <chronon3d/registry/text_preset_resolver.hpp>   // Cluster B public API surface
#include <chronon3d/registry/animator_resolver.hpp>      // TICKET-012 — header-lifted public AnimatorResolver.

#include <chronon3d/scene/builders/scene_builder.hpp>   // full SceneBuilder
#include <chronon3d/scene/builders/layer_builder.hpp>    // full LayerBuilder
#include <chronon3d/scene/builders/builder_params.hpp>   // full TextSpec (canonical)

#include <stdexcept>
#include <utility>      // std::move for wire_preset_text_run_params implementation.

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

// ── Forward declarations (TICKET-012 gcc + PCH workaround) ───────────────────
// Per C++ [basic.lookup], names declared in a namespace are visible from
// any point within that namespace body, including before their textual
// definition.  In standard C++ no forward declaration is required here.
// However, gcc with `-include cmake_pch.hxx -Winvalid-pch` emits a
// spurious "'wire_through_resolver' was not declared in this scope"
// diagnostic when `wire_through_resolver` is reached inside a
// non-template lambda's operator() body and only appears as a forward
// reference at that point in the same anonymous namespace.  Empirically
// the explicit declaration below resolves it.  There is NO C++ rule
// violation either way; this declaration is purely an empirical
// workaround for a downstream tooling quirk and can be removed if the
// underlying gcc issue is ever fixed (or the helper is reordered above
// its first call site).
[[nodiscard]] LayerBuilderT&
wire_through_resolver(LayerBuilderT& lb,
                      std::string_view preset_id,
                      const TextSpecT& spec);


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
        wire_through_resolver(lb, "animation_compositions", spec)
          .depth_reveal(280.0f, Frame{45})
          .soft_pop(Frame{30})
          .float_idle(8.0f, Frame{120});
    };
    return p;
}

// ── AnimatorResolver — header-lifted in TICKET-012 ──────────────
// The struct + its 22-branch `compose_for` table now live in
// `include/chronon3d/registry/animator_resolver.hpp`.
// See TICKET-012 in docs/FOLLOWUP_TICKETS.md.


// ── wire_through_resolver — factory-body helper (Cluster A bridge) ─────────
// Thin factory-body helper that delegates to the Cluster B public API
// (`wire_preset_text_run_params`) and then routes the returned
// TextRunParams through `lb.text_run(...).commit()`.  Single canonical
// pipeline: every text preset enters the render graph as a TextRunShape
// (ShapeType::TextRun), regardless of whether the resolver wired a
// TextAnimatorSpec or not.  Preserves Sub-case 7-9 invariants:
// minimal_white + unknown ids still produce 1 RenderNode via the
// empty-animators text_run path — `materialize_text_run_shape` synthesizes
// a valid shape when animators is non-empty AND when it is empty (the
// driver treats empty as a static no-op).
//
// Returns LayerBuilder& so the chained motion-presets in the factory
// body (.depth_reveal / .scale_drop / .soft_pop / etc.) continue to
// compile unchanged.
[[nodiscard]] inline LayerBuilderT&
wire_through_resolver(LayerBuilderT& lb,
                      std::string_view preset_id,
                      const TextSpecT& spec) {
    // Delegate to the public Cluster B free function — keeps the
    // resolver logic in a single place (the public header) so the
    // test harness and downstream authoring facade use the same
    // path the factory bodies here use.
    TextRunParams params =
        ::chronon3d::registry::wire_preset_text_run_params(preset_id, spec);
    const std::string entry_name = std::string{preset_id} + "_text";
    // P1 — single canonical text pipeline (no empty-animators shortcut to
    // lb.text()).  Use ONLY `lb.text_run(...).commit()` from this helper;
    // every preset entry — including minimal_white and unknown ids — flows
    // through the same single path.  Anti-duplication-guardrail
    // (CORE_OWNERSHIP.md §anti-duplicazione registry/resolver).
    return lb.text_run(entry_name, params).commit();
}

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

        // ── Stages 4 + 5 wiring ─────────────────────────────────────
        // Stage 4: when the spec carries a rich-paint signal
        //   (stroke_enabled OR fill_style OR stroke_style) the resolver
        //   pushes a TextAnimatorSpec with id "ctc_rich_<id>" first —
        //   the anchor that other content layers compose with.
        // Stage 5: ALL cinematic_text_camera invocations ALSO push the
        //   canonical end-state composition (animators[1] when rich,
        //   animators[0] when plain).  This makes the resolver output
        //   semantically rich — depth_reveal (260) -> PositionProperty{z},
        //   scale_drop (0.95) -> ScaleProperty{0.95}, soft_pop -> Opacity.
        //
        // Sub-case 29 invariant: when the spec IS rich, animators[0]
        //   keeps the rich-paint anchor id "ctc_rich_cinematic_text_camera"
        //   so the Stage 4 deterministic probe stays green.
        TextRunParams params;
        params.text = spec;
        if (::chronon3d::registry::AnimatorResolver::spec_is_rich(spec)) {
            params.animators.push_back(
                ::chronon3d::registry::AnimatorResolver::rich_paint_anchor("cinematic_text_camera"));
        }
        if (auto canonical = ::chronon3d::registry::AnimatorResolver::compose_for("cinematic_text_camera")) {
            params.animators.push_back(*canonical);
        }
        lb.text_run("camera_text", params)
          .commit()
          .depth_reveal(260.0f, Frame{50})
          .scale_drop(0.95f, Frame{30})
          .soft_pop(Frame{24});
    };
    return p;
}

TextPreset cinematic_title_reveal_entry() {
    TextPreset p;
    p.id           = "cinematic_title_reveal";
    p.display_name = "Cinematic title reveal (push-in/tilt variants)";
    p.category     = TextPresetCategory::Cinematic;
    p.description  = "Cinematic title reveal utilities — push-in + tilt "
                     "variants for hero/section titles with subtle drift. "
                     "AGENT 2 (TICKET-A2) — animation is resolver-driven "
                     "(Scale/Opacity/Blur/Position AnimatedValue over 36f "
                     "ease-out cubic); no layer-level chain so the canonical "
                     "resolver path is the single source of keyframes.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/cinematic_motion/cinematic_title_reveal
        // AGENT 2 — animation lives in AnimatorResolver::compose_for;
        // factory body only routes through the resolver (no second
        // motor of keyframes per AGENTS.md / brief A2.2+A2.3).
        // (void)-cast silences [[nodiscard]] on wire_through_resolver
        // (matches minimal_white_entry pattern at line ~547).
        (void)wire_through_resolver(lb, "cinematic_title_reveal", spec);
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
        wire_through_resolver(lb, "tilt_sweep_title_v2", spec)
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
        wire_through_resolver(lb, "text_animations", spec)
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
        wire_through_resolver(lb, "fade_in", spec)
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
        wire_through_resolver(lb, "blur_in", spec)
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
        wire_through_resolver(lb, "slide_up", spec)
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
        wire_through_resolver(lb, "slide_down", spec)
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
        wire_through_resolver(lb, "scale_in", spec)
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
                     "no entrance motion of its own. AGENT 2 (TICKET-A2) — "
                     "Tracking 0.18→0 over 35f and Opacity 0→1 are "
                     "resolver-driven (AnimatedValue<>); the factory body "
                     "is just the canonical wire-through-resolver path.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_tracking_close
        // AGENT 2 — resolver-driven; layer-level tracking_breathing removed
        // (it duplicated the resolver output, see A2.2 / A2.3).
        // (void)-cast silences [[nodiscard]] on wire_through_resolver
        // (matches minimal_white_entry pattern).
        (void)wire_through_resolver(lb, "tracking_close", spec);
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
        wire_through_resolver(lb, "masked_line_reveal", spec)
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
    p.description  = "Per-word stagger (Frame{3} delay) + fade_in.  Each "
                     "word enters in succession — the classic kinetic-title "
                     "reveal pattern. AGENT 2 (TICKET-A2) — the Word-unit "
                     "selector carries an AnimatedValue<>`end` that sweeps "
                     "0→100 over 48f, and OpacityProperty/PositionProperty/"
                     "ScaleProperty ramp 0→end_state over 36f ease-out cubic. "
                     "The factory routes through the resolver only — no "
                     "layer-level word_stagger + fade_in chain (single "
                     "canonical path, A2.3).";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_word_cascade
        // AGENT 2 — resolver-driven; layer-level chain removed (A2.3).
        // (void)-cast silences [[nodiscard]] on wire_through_resolver
        // (matches minimal_white_entry pattern).
        (void)wire_through_resolver(lb, "word_cascade", spec);
    };
    return p;
}

// 10. character_cascade — same recipe as word_cascade but with Frame{1}
//     per-glyph delay.  Use when the input has clear glyph separators
//     (monospace, kerned display fonts).
TextPreset character_cascade_entry() {
    TextPreset p;
    p.id           = "character_cascade";
    p.display_name = "CharacterCascade";
    p.category     = TextPresetCategory::Reveal;
    p.description  = "Per-glyph stagger (Frame{1} delay) + fade_in.  Tighter "
                     "than WordCascade — best on monospace / kerned display. "
                     "AGENT 2 (TICKET-A2) — Glyph-unit selector with "
                     "AnimatedValue<>`end` (24f) + property ramps over 18f "
                     "ease-out cubic; factory routes through the resolver "
                     "only — no layer-level fade_in + word_stagger chain "
                     "(single canonical path, A2.3).";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/reveal_character_cascade
        // AGENT 2 — resolver-driven; layer-level chain removed (A2.3).
        // (void)-cast silences [[nodiscard]] on wire_through_resolver
        // (matches minimal_white_entry pattern).
        (void)wire_through_resolver(lb, "character_cascade", spec);
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
        wire_through_resolver(lb, "word_pop", spec)
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
        wire_through_resolver(lb, "scale_punch", spec)
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
        wire_through_resolver(lb, "color_accent", spec)
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
        wire_through_resolver(lb, "gradient_fill", spec)
          .fade_shift_vertical(Vec3{0.0f, 80.0f, 0.0f}, Frame{20})
          .fade_in(Frame{15});
    };
    return p;
}


// ── Subtitle factory functions (Stage 3 — 4 addizionali) ───────────────────

// 19. minimal_white — the no-motion subtitle baseline. The builder
//     routes through `wire_through_resolver(...)` (Cluster A bridge)
//     like all other 21 presets, so the canonical pipeline is uniform.
//     No entrance / no idle motion: use for static captions or
//     burnt-in subs.
TextPreset minimal_white_entry() {
    TextPreset p;
    p.id           = "minimal_white";
    p.display_name = "MinimalWhite";
    p.category     = TextPresetCategory::Subtitle;
    p.description  = "No-motion baseline. Routes through `wire_through_resolver` "
                     "(single canonical path) — empty animators produce a "
                     "static TextRunNode via `materialize_text_run_shape`.  "
                     "Use for static captions or burnt-in subs.";
    p.builtin      = true;
    p.builder      = []([[maybe_unused]] SceneBuilderT& sb,
                        LayerBuilderT& lb,
                        const TextSpecT& spec) {
        // golden-frame-link: tests/visual/text/subtitle_minimal_white
        // P1 — minimal_white now goes through the same path as the other
        // 21 built-ins (Sub-cases 7-9 invariants preserved: 1 RenderNode
        // produced when params.animators.empty()).
        // The (void) cast is intentional — wire_through_resolver is
        // [[nodiscard]] for the 21 chained callers (.depth_reveal(...) etc.)
        // and minimal_white does not chain, so we explicitly discard.
        (void)wire_through_resolver(lb, "minimal_white", spec);
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
        wire_through_resolver(lb, "yellow_keyword", spec)
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
        wire_through_resolver(lb, "glow_pulse", spec)
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
        wire_through_resolver(lb, "caption_box", spec)
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
// ── wire_preset_text_run_params — Cluster B public free function ─────────
// Public Cluster B API surface entry point.  Declared in
// include/chronon3d/registry/text_preset_resolver.hpp; see that header
// for full doc + downstream usage patterns.
//
// Implementation lives in `::chronon3d::registry` namespace (NOT anon)
// so cross-TU callers — the test harness in tests/test_text_preset_registry.cpp
// and any downstream Cluster B authoring facade — can link to the
// external symbol.  The body calls `::chronon3d::registry::AnimatorResolver::compose_for()`
// which is in the anon namespace above; anon-namespace symbols are
// visible by standard name lookup from the enclosing
// `::chronon3d::registry` namespace within the same TU (C++ [namespace]).
//
// Return value contract:
//   - `params.text = std::move(spec)` — the supplied spec moved into
//     `out.text` (always populated, regardless of motion presence).
//   - `params.animators` — pushed exactly ONE entry (the composed
//     TextAnimatorSpec) when the resolver returns a spec; EMPTY when
//     the preset has no canonical motion (`minimal_white`) OR when
//     `preset_id` is not in the registered catalog.
//   - Callers detect the no-motion case via `params.animators.empty()`
//     rather than a std::optional wrapper — keeps downstream authoring
//     facades ergonomic.
TextRunParams
wire_preset_text_run_params(std::string_view preset_id,
                            TextSpec spec) noexcept {
    auto composed = ::chronon3d::registry::AnimatorResolver::compose_for(preset_id);
    TextRunParams out;
    out.text = std::move(spec);
    if (composed) {
        out.animators.push_back(std::move(*composed));
    }
    return out;
}

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_builtin_presets(r);
    return r;
}

} // namespace chronon3d::registry
