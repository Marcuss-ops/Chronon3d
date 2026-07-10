#pragma once

// ── Centered Text Helpers ──────────────────────────────────────────────────
// Extracted from text_helpers.hpp.
// centered_text(), glow_text(), compute_single_line_glyph_layout()

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/text_definition.hpp>  // F2.C — canonical DTO
#include <string>
#include <utility>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
// CenterTextOptions — unified options struct for all text helpers
// ═════════════════════════════════════════════════════════════════════════════

struct CenterTextOptions {
    std::string text;                     // text content (consumed)
    Vec2  box{1200.0f, 240.0f};           // logical text box
    Vec3  pos{0.0f, 0.0f, 0.0f};          // position (anchor is always Center)
    std::string font_asset{"assets/fonts/Poppins-Bold.ttf"};  // asset-relative path
    std::string font_family{"Poppins"};
    int   font_weight{700};
    std::string font_style{"normal"};
    f32   font_size{96.0f};               // target font size
    f32   tracking{0.0f};                 // extra px per glyph
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int   max_lines{1};                   // 0 = unlimited
    bool  auto_fit{false};                 // shrink to fit box
    f32   line_height{0.95f};             // multiplier of font-size
    f32   min_font_size{12.0f};           // floor for auto-fit
    f32   max_font_size{160.0f};          // ceiling for auto-fit
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. centered_text
// ═════════════════════════════════════════════════════════════════════════════

/// F2.C — canonical authoring helper.  Returns TextDefinition, the single
/// canonical authoring DTO.  Composes directly with LayerBuilder::text().
inline TextDefinition centered_text(CenterTextOptions o) {
    return from_text_spec(TextSpec{
        .content    = {.value = std::move(o.text)},
        .font       = {.font_path   = std::move(o.font_asset),
                       .font_family = std::move(o.font_family),
                       .font_weight = o.font_weight,
                       .font_style  = std::move(o.font_style),
                       .font_size   = o.font_size},
        .layout     = {.box            = o.box,
                       .anchor         = TextAnchor::Center,
                       .centering_mode = TextCenteringMode::PixelInk,
                       .align          = TextAlign::Center,
                       .vertical_align = VerticalAlign::Middle,
                       .wrap           = TextWrap::Word,
                       .overflow       = TextOverflow::Clip,
                       .line_height    = o.line_height,
                       .tracking       = o.tracking,
                       .auto_fit       = o.auto_fit,
                       .min_font_size  = o.min_font_size,
                       .max_font_size  = o.max_font_size,
                       .max_lines      = o.max_lines},
        .appearance = {.color = o.color},
        .placement = {TextPlacementKind::Absolute},
        .offset    = {o.pos.x, o.pos.y},
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. glow_text — DEPRECATED since F2 (2026-07-10) + Phase A5 (canonical route)
// ═════════════════════════════════════════════════════════════════════════════
//
// ⚠️ DEPRECATED: use centered_text() + set .style.material.use_material_glow +
// .style.material.glow_{color,radius,intensity} on the returned TextDefinition.
// `TextMaterial` (the umbrella material struct) is the SINGLE canonical
// compositor surface for glow/bevel effects — there is no duplicate
// `TextEffects` field on `TextDefinition` (eliminated in Phase A5 close-out).
//
// Migration:
//   // Before (Phase A.3 → Phase A5):
//   auto def = glow_text(opts, glow_color, radius, intensity);
//   // After (Phase A5 canonical):
//   auto def = centered_text(opts);
//   def.style.material.use_material_glow = true;
//   def.style.material.glow_color        = glow_color;
//   def.style.material.glow_radius       = radius;
//   def.style.material.glow_intensity    = intensity;
//
/// F2 + Phase A5 — wires glow parameters to TextMaterial.use_material_glow +
/// TextMaterial.glow_{color,radius,intensity} on the returned TextDefinition.
/// `TextMaterial` is the single canonical compositor surface; the prior
/// `TextEffects` duplicate struct was eliminated in Phase A5 close-out.
[[deprecated("Use centered_text() + set .style.material.{use_material_glow,glow_*} "
             "on TextDefinition instead (Phase A5 canonical seam)")]]
inline TextDefinition glow_text(CenterTextOptions o,
                            Color glow_color = {1.0f, 1.0f, 1.0f, 1.0f},
                            f32 radius = 24.0f,
                            f32 intensity = 0.6f) {
    auto def = centered_text(std::move(o));
    // Phase A5 canonical route (was: def.effects.* in F2/Phase A.3).
    // TextMaterial is the single canonical compositor surface.
    def.style.material.use_material_glow = true;
    def.style.material.glow_color        = glow_color;
    def.style.material.glow_radius       = radius;
    def.style.material.glow_intensity    = intensity;
    return def;
}

} // namespace chronon3d::content::text
