#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/ae_parity/glow_final_compositions.hpp
//
// TICKET-CHRONON-GLOW-FINAL — Phase 1 unified factory.
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
// Three public entry points live in `chronon3d::test::glow_final`:
//   make_chronon_glow_final(props)                 — CLI factory (auto FontEngine)
//   make_chronon_glow_final_for_test(props, Fe&)   — test factory (explicit Fe)
//   build_chronon_glow_scene(SB&, props, frame, Fe*) — shared inner composer
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API in include/.
// All surface lives in tests/.
// Header-only / inline (no link-time surface, no CMake target surface).
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

// ── Composition parameter struct ────────────────────────────────────────
struct ChrononGlowProps {
    std::string            text{"PULSE GLOW"};
    bool                   portrait{false};
    bool                   glow_enabled{true};
    chronon3d::f32         glow_strength{kDefaultGlowStrength};
    // Toggle the subtle scale breath (0.96/1.05/0.98 multiplier).
    // Default off so the helper matches the legacy ae_08_glow_pulse CLI
    // factory (which deliberately omits l.scale() because non-identity
    // scale triggers use_local and skips canvas center bake).  Golden
    // tests opt-in (they originally captured pngs WITH the breath).
    bool                   enable_scale_breath{false};

    // Layout (overridable per AR).
    chronon3d::f32         font_size{230.0f};
    chronon3d::Vec2        box{1700.0f, 360.0f};          // landscape default
    chronon3d::Vec2        canvas_size{1920.0f, 1080.0f}; // landscape default
};

// ── AR-specific defaults ─────────────────────────────────────────────────
inline ChrononGlowProps default_landscape_props() {
    return ChrononGlowProps{
        .text                = "PULSE GLOW",
        .portrait            = false,
        .glow_enabled        = true,
        .glow_strength       = kDefaultGlowStrength,
        .enable_scale_breath = false,  // legacy CLI semantics (no scale)
        .font_size           = chronon3d::f32{230.0f},
        .box                 = chronon3d::Vec2{1700.0f, 360.0f},
        .canvas_size         = chronon3d::Vec2{1920.0f, 1080.0f},
    };
}

inline ChrononGlowProps default_portrait_props() {
    return ChrononGlowProps{
        .text                = "PULSE GLOW",
        .portrait            = true,
        .glow_enabled        = true,
        .glow_strength       = kDefaultGlowStrength,
        .enable_scale_breath = false,  // legacy CLI semantics (no scale)
        .font_size           = chronon3d::f32{160.0f},
        .box                 = chronon3d::Vec2{1000.0f, 280.0f},
        .canvas_size         = chronon3d::Vec2{1080.0f, 1920.0f},
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
    const chronon3d::f32 opacity = opacity_for_frame(frame_idx);
    const chronon3d::Vec3 scale  = scale_for_frame(frame_idx);
    const chronon3d::Vec2 center = props.canvas_size * 0.5f;
    const bool apply_breath = props.enable_scale_breath;

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
                // The previous `Absolute {center.x, center.y}` form
                // produced a centroid that drifted by ~5% of the bbox
                // radius under non-identity scale (the symptom that
                // Fase 1 deferred with `enable_scale_breath=false`).
                .placement  = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::CanvasCenter,
                    {},
                },
                .font = {
                    .font_path   = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size   = props.font_size,
                },
                .layout = {
                    .box            = props.box,
                    .align          = chronon3d::TextAlign::Center,
                    .vertical_align = chronon3d::VerticalAlign::Middle,
                },
                .appearance = {.color = chronon3d::Color::white()},
            },
        }).commit();
        // Per-frame envelope: opacity always; scale gated by the
        // enable_scale_breath flag (legacy ae_08_glow_pulse CLI omits
        // the scale breath to keep canvas-center bake).
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

// ── CLI factory — SceneBuilder auto-forwards pipeline FontEngine ────────
inline chronon3d::Composition make_chronon_glow_final(ChrononGlowProps props) {
    const chronon3d::Vec2 cs = props.canvas_size;
    const std::string name = props.portrait
        ? std::string{"ChrononGlowFinal/9x16"}
        : std::string{"ChrononGlowFinal/16x9"};
    const unsigned w = static_cast<unsigned>(cs.x);
    const unsigned h = static_cast<unsigned>(cs.y);
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

// ── Test factory — explicit FontEngine reference (test-renderer atlas) ──
inline chronon3d::Composition make_chronon_glow_final_for_test(
        ChrononGlowProps props,
        chronon3d::FontEngine& engine) {
    const chronon3d::Vec2 cs = props.canvas_size;
    const std::string name = props.portrait
        ? std::string{"ChrononGlowFinal/9x16"}
        : std::string{"ChrononGlowFinal/16x9"};
    const unsigned w = static_cast<unsigned>(cs.x);
    const unsigned h = static_cast<unsigned>(cs.y);
    return chronon3d::composition(
        {
            .name       = name,
            .width      = w,
            .height     = h,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration   = 60,
        },
        [props, &engine](const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            build_chronon_glow_scene(
                s, props, ctx.frame.integral(), &engine);
            return s.build();
        });
}

} // namespace chronon3d::test::glow_final
