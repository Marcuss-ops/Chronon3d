// ═══════════════════════════════════════════════════════════════════════════
// content/compositions/chronon_glow_final.cpp
//
// TICKET-CHRONON-GLOW-FINAL — implementation companion for the slim decl-only
// header.  Pairs with `content/compositions/chronon_glow_final.hpp`.
//
// All body that previously lived `inline` in the old header has been moved
// here.  The 3 public symbols are defined out-of-line in the
// `chronon3d::content::glow_final` namespace (matching the header decls):
//
//   default_landscape_props()
//   default_portrait_props()
//   make_chronon_glow_final(props)
//
// All helper internals (numeric Phase-2 constants, GlowLayout resolver,
// cinematic-glow preset builder, per-frame opacity / scale envelope, the
// inner scene composer `build_chronon_glow_scene`) live in the anonymous
// namespace at the top of this TU — purely internal, no link-time surface,
// no risk of ODR collision with siblings.
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API in include/.
// All surface lives in content/.  Implementation lives here; the link-time
// surface is added by `content/CMakeLists.txt` registering this file in
// the `chronon3d_content` OBJECT library.
//
#include "content/compositions/chronon_glow_final.hpp"
// Header brings: kDefaultGlowStrength, GlowFormat, ChrononGlowProps, and
// 3 public-function decls (default_landscape_props, default_portrait_props,
// make_chronon_glow_final).  Public surface contract.

#include <cstddef>
#include <string>

#include <chronon3d/core/types/frame.hpp>
// chronon3d::f32, chronon3d::FrameRate, etc. — used by the factory body.

#include <chronon3d/core/types/frame_context.hpp>
// chronon3d::FrameContext — captured-by-value in the composition lambda.

#include <chronon3d/scene/builders/builder_params.hpp>
// TextDefinition / TextRunSpec / TextPlacementKind::CanvasCenter /
// TextAlign / VerticalAlign / Vec2 / Vec3 / Color — used by the inner
// scene composer (build_chronon_glow_scene below).

#include <chronon3d/scene/builders/scene_builder.hpp>
// chronon3d::SceneBuilder — the composer is built around it.

#include <chronon3d/scene/builders/layer_builder.hpp>
// chronon3d::LayerBuilder — the inner lambda receives one of these.

#include <chronon3d/text/text_run_shape.hpp>
// chronon3d::TextRunSpec — used in the composer below.

#include <chronon3d/timeline/composition.hpp>
// chronon3d::composition / chronon3d::Composition / Scene — return type
// of the factory and the lambda return type used by composition(...).

#include "content/common/text/cinematic_glow.hpp"
#include <chronon3d/text/text_definition.hpp>
// chronon3d::content::text_reveal::apply_cinematic_glow +
// CinematicGlowPreset — invoked inside build_chronon_glow_scene's
// `s.layer("hero", ...)` lambda when `props.glow_enabled`.

namespace {

using chronon3d::content::glow_final::ChrononGlowProps;
using chronon3d::content::glow_final::GlowFormat;

// ── Spec numeric constants (Phase 2 §spec — keep aligned, TU-local) ─────
//
// Lives in anonymous namespace so each TU has its own copy with internal
// linkage (no ODR constraint).  `kDefaultGlowStrength` is the only constant
// that lives in the .hpp because it is the in-class default initializer
// for `ChrononGlowProps::glow_strength` and must therefore be visible at
// struct-definition time.
constexpr chronon3d::f32 kMidOverInnerRatio =
    chronon3d::f32{0.22f} / chronon3d::f32{0.55f};   // ≈ 0.40
constexpr chronon3d::f32 kBloomOverInnerRatio =
    chronon3d::f32{0.08f} / chronon3d::f32{0.55f};   // ≈ 0.14545
constexpr chronon3d::f32 kGlowInnerRadiusPx =
    chronon3d::f32{4.0f};
constexpr chronon3d::f32 kGlowMidRadiusPx =
    chronon3d::f32{14.0f};
constexpr chronon3d::f32 kGlowBloomRadiusPx =
    chronon3d::f32{34.0f};

// ── Layout resolver (Step 8 §B: single source of truth) ────────────────
//
// `GlowLayout` and `resolve_layout` are TU-local — they exist only to
// derive canvas_size / font_size / box from `GlowFormat`.  External callers
// never touch this struct directly; the derived values flow into
// `build_chronon_glow_scene` below.
struct GlowLayout {
    chronon3d::Vec2 canvas_size;
    chronon3d::f32  font_size;
    chronon3d::Vec2 box;
};

GlowLayout resolve_layout(GlowFormat format) {
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

// ── Cinematic glow preset helper (Phase 2 §spec, TU-local) ────────────
chronon3d::content::text_reveal::CinematicGlowPreset
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
chronon3d::f32 opacity_for_frame(long frame_idx) {
    if (frame_idx <= 0)  return chronon3d::f32{0.40f};
    if (frame_idx <= 15) return chronon3d::f32{0.85f};
    return                     chronon3d::f32{0.50f};
}

chronon3d::Vec3 scale_for_frame(long frame_idx) {
    if (frame_idx <= 0)  return chronon3d::Vec3{0.96f, 0.96f, 1.0f};
    if (frame_idx <= 15) return chronon3d::Vec3{1.05f, 1.05f, 1.0f};
    return                     chronon3d::Vec3{0.98f, 0.98f, 1.0f};
}

// ── Inner scene composer (TU-local; shared between factory + future detours) ──
//
// Builds a single named layer "hero" with the text animated via
// `opacity_for_frame()` + `scale_for_frame()` and optionally wrapped in
// the Phase 2 cinematic glow pipeline.  Centred on `layout.canvas_size / 2`
// via `TextPlacementKind::CanvasCenter` placement (Phase 3 SCALA fix).
//
// `frame_idx` is Frame::integral(); passes 0/15/30 directly for snapshot
// tests, or the live ctx.frame.integral() for CLI runtime.
//
// Action 14/2 (closed on this commit): the prior `FontEngine* engine`
// parameter has been REMOVED — the Sole canonical call site
// (`make_chronon_glow_final` lambda below) was always passing
// `/*engine=*/nullptr`, which means SceneBuilder was always auto-
// forwarding the pipeline FontEngine (Step 8 §C path).  No real caller
// passed a non-null engine after the test factory
// `make_chronon_glow_final_for_test` was retired in Step 8 §C.  The
// two `if (engine) { ... }` guards are therefore dead-branches and
// have been removed alongside the parameter.
void build_chronon_glow_scene(
        chronon3d::SceneBuilder& s,
        const ChrononGlowProps& props,
        long frame_idx) {
    // Step 8 §B: single source of truth for layout (derived from format).
    const GlowLayout layout = resolve_layout(props.format);
    const chronon3d::f32 opacity = opacity_for_frame(frame_idx);
    const chronon3d::Vec3 scale  = scale_for_frame(frame_idx);
    const bool apply_breath = props.scale_breath;

    s.layer("hero", [&, opacity, scale, apply_breath](chronon3d::LayerBuilder& l) {
        l.text("glow_pulse", chronon3d::TextDefinition{
    .content = {.value = props.text},
    .style = {
        .font = {
                    .font_path   = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size   = layout.font_size,
                },
        .color = chronon3d::Color::white()
    },
    .frame = {.size = layout.box,
.placement = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::CanvasCenter,
                    {},
                },
.align = chronon3d::TextAlign::Center,
.vertical_align = chronon3d::VerticalAlign::Middle
    }
});
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

} // namespace (TU-local helpers)

// ═══════════════════════════════════════════════════════════════════════════
// Public surface — out-of-line definitions matching the 3 decls in
// `content/compositions/chronon_glow_final.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::content::glow_final {

ChrononGlowProps default_landscape_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Landscape,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

ChrononGlowProps default_portrait_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Portrait,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

chronon3d::Composition make_chronon_glow_final(ChrononGlowProps props) {
    const GlowLayout layout = resolve_layout(props.format);
    const std::string name = (props.format == GlowFormat::Portrait)
        ? std::string{"ChrononGlowFinal/9x16"}
        : std::string{"ChrononGlowFinal/16x9"};
    const chronon3d::i32 w = static_cast<chronon3d::i32>(layout.canvas_size.x);
    const chronon3d::i32 h = static_cast<chronon3d::i32>(layout.canvas_size.y);
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
                s, props, ctx.frame.integral());
            return s.build();
        });
}

} // namespace chronon3d::content::glow_final
