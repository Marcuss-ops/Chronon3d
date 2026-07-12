#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// content/compositions/chronon_glow_final.hpp
//
// TICKET-CHRONON-GLOW-FINAL — Phase 1 unified factory (production location).
//
// Originally landed at `tests/visual/ae_parity/glow_final_compositions.hpp`
// (commit `cd42bc97` Fase 1 closure, 2026-07-11).  Re-homed to the canonical
// `content/compositions/` production-content directory on 2026-07-12 per
// the Step 7 refactor(glow) §4 cleanup mandate — the helper has graduated
// from "test-only scaffolding used incidentally by the CLI" to "canonical
// production cinematic-glow factory exposed as `chronon3d_cli render
// ChrononGlowFinalAE`", and its physical location now reflects that role.
//
// Replaces three duplicated implementations of the ae_08 cinematic glow
// composition:
//   1. tests/visual/ae_parity/ae_parity_compositions.cpp       (CLI factory)
//   2. tests/text_golden/ae_parity/ae_08_glow_pulse.cpp        (golden landscape)
//   3. tests/text_golden/ae_parity/ae_08_glow_pulse.cpp        (golden portrait)
//
// with a single header-only helper that
//   (a) applies the canonical Phase 2 cinematic glow (radii 4/14/34 px,
//       intensities 0.55/0.22/0.08, micro_shadow=true) via
//       content::text_reveal::apply_cinematic_glow when `props.glow_enabled`,
//   (b) animates per-frame opacity 0.40 → 0.85 → 0.50 plus uniform scale
//       0.96 → 1.05 → 0.98 to match the 0/15/30 golden snapshot buckets,
//   (c) centres on the canvas viewport per AR (16:9 → 960,540; 9:16 → 540,960).
//
// One public entry point lives in `chronon3d::test::glow_final` (Step 8 §C):
//   make_chronon_glow_final(props)                       — production factory (auto FontEngine)
//   build_chronon_glow_scene(SB&, props, frame, Fe*)      — shared inner composer
//
// (The prior test factory `make_chronon_glow_final_for_test(props, Fe&)` is
// REMOVED in Step 8 §C; tests now call the production factory with
// `engine=nullptr` and rely on `make_renderer_shared()` + SceneBuilder
// default font engine — the canonical pipeline path.)
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API in include/.
// All surface lives in content/.  Header-only / inline (no link-time surface,
// no CMake target surface).  Step 8 §B: 5-field ChrononGlowProps (no
// portrait flag, no font_size/box/canvas_size members — derived from
// GlowFormat via resolve_layout).  Step 8 §C: single production factory
// (test factory removed).
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <string>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "content/common/text_reveal_helpers.hpp"

namespace chronon3d::test::glow_final {

// ── Spec numeric constants (Phase 2 §spec — keep aligned) ────────────────
inline constexpr chronon3d::f32 kDefaultGlowStrength =
    chronon3d::f32{0.55f};
inline constexpr chronon3d::f32 kMidOverInnerRatio =
    chronon3d::f32{0.22f} / chronon3d::f32{0.55f};   // ≈ 0.40
inline constexpr chronon3d::f32 kBloomOverInnerRatio =
    chronon3d::f32{0.08f} / chronon3d::f32{0.55f};   // ≈ 0.14545
inline constexpr chronon3d::f32 kGlowInnerRadiusPx =
    chronon3d::f32{4.0f};
inline constexpr chronon3d::f32 kGlowMidRadiusPx =
    chronon3d::f32{14.0f};
inline constexpr chronon3d::f32 kGlowBloomRadiusPx =
    chronon3d::f32{34.0f};

// ── Format enum (Step 8 §B: replaces the bool portrait flag) ────────────
enum class GlowFormat {
    Landscape,
    Portrait,
};

// ── Layout resolver (single source of truth for canvas_size, font_size,
//    box — derived from GlowFormat; impossible to construct
//    `portrait=true, canvas_size={1920,1080}` because canvas_size is
//    derived from format, not stored in ChrononGlowProps). ───────────────
struct GlowLayout {
    chronon3d::Vec2 canvas_size;
    chronon3d::f32  font_size;
    chronon3d::Vec2 box;
};

inline GlowLayout resolve_layout(GlowFormat format) {
    switch (format) {
        case GlowFormat::Landscape:
            return GlowLayout{
                chronon3d::Vec2{1920.0f, 1080.0f},
                chronon3d::f32{230.0f},
                chronon3d::Vec2{1700.0f, 360.0f},
            };
        case GlowFormat::Portrait:
            return GlowLayout{
                chronon3d::Vec2{1080.0f, 1920.0f},
                chronon3d::f32{160.0f},
                chronon3d::Vec2{1000.0f, 280.0f},
            };
    }
    return GlowLayout{};  // unreachable (suppress -Wreturn-type)
}

// ── Composition parameter struct ────────────────────────────────────────
struct ChrononGlowProps {
    // Step 8 §B: 5-field simplified struct.  No `portrait` flag,
    // no `font_size` / `box` / `canvas_size` (derived from `format` via
    // resolve_layout).  No `enable_scale_breath` (renamed to `scale_breath`,
    // default true per user spec — Phase 3 SCALA CanvasCenter bake makes
    // non-identity scale safe; the legacy off-default is no longer needed).
    std::string            text{"PULSE GLOW"};
    GlowFormat             format{GlowFormat::Landscape};
    bool                   glow_enabled{true};
    chronon3d::f32         glow_strength{kDefaultGlowStrength};
    bool                   scale_breath{true};
};

// ── AR-specific defaults ─────────────────────────────────────────────────
inline ChrononGlowProps default_landscape_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Landscape,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

inline ChrononGlowProps default_portrait_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Portrait,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

// ── Cinematic glow preset helper (Phase 2 §spec) ───────────────────────
inline chronon3d::content::text_reveal::CinematicGlowPreset
default_cinematic_preset(float strength) {
    return chronon3d::content::text_reveal::CinematicGlowPreset{
        .inner_radius    = kGlowInnerRadiusPx,
        .mid_radius      = kGlowMidRadiusPx,
        .bloom_radius    = kGlowBloomRadiusPx,
        .inner_intensity = strength,
        .mid_intensity   = strength * kMidOverInnerRatio,
        .bloom_intensity = strength * kBloomOverInnerRatio,
        .micro_shadow    = true,
    };
}

// ── Per-frame envelope (matches the 0/15/30 snapshot buckets) ───────────
inline chronon3d::f32 opacity_for_frame(long frame_idx) {
    if (frame_idx <= 0)  return chronon3d::f32{0.40f};
    if (frame_idx <= 15) return chronon3d::f32{0.85f};
    return                     chronon3d::f32{0.50f};
}

inline chronon3d::Vec3 scale_for_frame(long frame_idx) {
    if (frame_idx <= 0)  return chronon3d::Vec3{0.96f, 0.96f, 1.0f};
    if (frame_idx <= 15) return chronon3d::Vec3{1.05f, 1.05f, 1.0f};
    return                     chronon3d::Vec3{0.98f, 0.98f, 1.0f};
}

// ── Inner scene composer (shared between CLI + test factories) ──────────
//
// Builds a single named layer "hero" with the text animated via
// `opacity_for_frame()` + `scale_for_frame()` and optionally wrapped in
// the Phase 2 cinematic glow pipeline.  Centred on `props.canvas_size / 2`.
//
// `frame_idx` is Frame::integral(); passes 0/15/30 directly for snapshot
// tests, or the live ctx.frame.integral() for CLI runtime.
//
// `engine` may be nullptr — SceneBuilder auto-forwards the pipeline
// FontEngine in that case.  When non-null, both SceneBuilder and the
// pushed layer receive the engine pointer explicitly (matches the
// golden-test pattern: l.font_engine(&renderer.font_engine())).
inline void build_chronon_glow_scene(
        chronon3d::SceneBuilder& s,
        const ChrononGlowProps& props,
        long frame_idx,
        chronon3d::FontEngine* engine) {
    if (engine) {
        s.font_engine(engine);
    }
    // Step 8 §B: single source of truth for layout (derived from format).
    const GlowLayout layout = resolve_layout(props.format);
    const chronon3d::f32 opacity = opacity_for_frame(frame_idx);
    const chronon3d::Vec3 scale  = scale_for_frame(frame_idx);
    const chronon3d::Vec2 center = layout.canvas_size * 0.5f;
    const bool apply_breath = props.scale_breath;

    s.layer("hero", [&, opacity, scale, apply_breath](chronon3d::LayerBuilder& l) {
        if (engine) {
            l.font_engine(engine);
        }
        l.text_run("glow_pulse", chronon3d::TextRunSpec{
            .text = chronon3d::TextSpec{
                .content    = {.value = props.text},
                // TICKET-CHRONON-GLOW-FINAL — Phase 3 SCALA:
                // `CanvasCenter` makes the text resolver bake the box
                // absolutely at the canvas centroid at compose time, so
                // subsequent non-identity layer transforms (e.g.
                // `l.scale(Vec3{0.96, 0.96, 1.0})` for the cinematic breath)
                // scale the text AND glow uniformly around the canvas
                // centroid without drifting it.  This is the idiomatic
                // fix — `pin_to(Anchor::Center)` would mix layer-coord
                // anchoring with text authoring and is rejected by
                // `tools/check_no_dual_text_api.sh`.
                // Step 8 §B: scale_breath default is now `true` (Phase 3
                // SCALA fix means the canvas-center bake ignores layer
                // scale, so the breath is safe by default).
                .placement  = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::CanvasCenter,
                    {},
                },
                .font = {
                    .font_path   = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size   = layout.font_size,
                },
                .layout = {
                    .box            = layout.box,
                    .align          = chronon3d::TextAlign::Center,
                    .vertical_align = chronon3d::VerticalAlign::Middle,
                },
                .appearance = {.color = chronon3d::Color::white()},
            },
        }).commit();
        // Per-frame envelope: opacity always; scale gated by the
        // scale_breath flag (Phase 3 SCALA fix means non-identity scale
        // does not break the canvas-center bake).
        l.opacity(opacity);
        if (apply_breath) {
            l.scale(scale);
        }

        // Phase 2 cinematic glow (optional, gated by props.glow_enabled).
        if (props.glow_enabled) {
            chronon3d::content::text_reveal::apply_cinematic_glow(
                l, default_cinematic_preset(props.glow_strength));
        }
    });
}

// ── Production factory (Step 8 §C: single factory) ────────────────────
//
// Uses SceneBuilder auto-forward for the pipeline FontEngine: when
// `engine == nullptr`, the SceneBuilder uses its default font engine
// (which is the renderer's font engine in the test harness via
// `make_renderer_shared()` + `attach_software_backend(renderer)`).
// This makes the production factory usable in both CLI and test paths —
// the test factory `make_chronon_glow_final_for_test` is removed.
inline chronon3d::Composition make_chronon_glow_final(ChrononGlowProps props) {
    const GlowLayout layout = resolve_layout(props.format);
    const std::string name = (props.format == GlowFormat::Portrait)
        ? std::string{"ChrononGlowFinal/9x16"}
        : std::string{"ChrononGlowFinal/16x9"};
    const unsigned w = static_cast<unsigned>(layout.canvas_size.x);
    const unsigned h = static_cast<unsigned>(layout.canvas_size.y);
    return chronon3d::composition(
        {
            .name       = name,
            .width      = w,
            .height     = h,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration   = 60,
        },
        [props](const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            build_chronon_glow_scene(
                s, props, ctx.frame.integral(), /*engine=*/nullptr);
            return s.build();
        });
}

} // namespace chronon3d::test::glow_final
