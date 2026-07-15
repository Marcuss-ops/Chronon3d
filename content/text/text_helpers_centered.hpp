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

// TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX — explicit parent-namespace
// imports.  These types live in `chronon3d::` (per builder_params.hpp +
// text_definition.hpp + frame_context.hpp); the deeper
// `chronon3d::content::text` namespace relies on basic.lookup.unqual to
// find them, but the explicit `using` declarations below lock the
// dependency at compile time (vs relying on lookup cascading) so a
// parent-namespace rename is caught immediately.
using chronon3d::f32;
using chronon3d::u32;
using chronon3d::Vec2;
using chronon3d::Vec3;
using chronon3d::Color;
using chronon3d::TextDefinition;
using chronon3d::TextPlacement;
using chronon3d::TextPlacementKind;
using chronon3d::TextAnchor;
using chronon3d::TextAlign;
using chronon3d::VerticalAlign;
using chronon3d::TextWrap;
using chronon3d::TextOverflow;
using chronon3d::TextCenteringMode;

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

/// F2.C — DEPRECATED since Blocco 5.1 (2026-07-14).  Returns TextDefinition
/// (the single canonical authoring DTO), but the wrapper itself is removed
/// after the migration lands.  Use direct TextDefinition construction
/// instead (per the TICKET-CENTERED-TEXT-MIGRATION forward-point).
[[deprecated("Use direct TextDefinition construction (this helper is removed after the migration lands) — "
             "see TICKET-CENTERED-TEXT-MIGRATION (Blocco 5.1).")]]
inline TextDefinition centered_text(CenterTextOptions o) {
    return TextDefinition{
    .content = {.value = std::move(o.text)},
    .style = {
        .font = {.font_path   = std::move(o.font_asset),
                       .font_family = std::move(o.font_family),
                       .font_weight = o.font_weight,
                       .font_style  = std::move(o.font_style),
                       .font_size   = o.font_size},
        .color = o.color
    },
    .frame = {.size = o.box,
.placement = TextPlacement{TextPlacementKind::Absolute, {o.pos.x, o.pos.y}},
.anchor = TextAnchor::Center,
.align = TextAlign::Center,
.vertical_align = VerticalAlign::Middle,
.wrap = TextWrap::Word,
.overflow = TextOverflow::Clip,
.centering_mode = TextCenteringMode::PixelInk,
.line_height = o.line_height,
.tracking = o.tracking,
.auto_fit = o.auto_fit,
.min_font_size = o.min_font_size,
.max_font_size = o.max_font_size,
.max_lines = o.max_lines
    }
};
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. glow_text — DEPRECATED since F2 (2026-07-10) + Phase A5 (canonical route)
// ═════════════════════════════════════════════════════════════════════════════
//
// ⚠️ DEPRECATED (Blocco 5.1, 2026-07-14): use direct `TextDefinition`
// construction + set .style.material.use_material_glow +
// .style.material.glow_{color,radius,intensity} on the returned DTO.
// `TextMaterial` (the umbrella material struct) is the SINGLE canonical
// compositor surface for glow/bevel effects — there is no duplicate
// `TextEffects` field on `TextDefinition` (eliminated in Phase A5 close-out).
// Forward-point: TICKET-CENTERED-TEXT-MIGRATION (P2, OPEN).
//
// Migration:
//   // Before (Phase A.3 → Phase A5 → Blocco 5.1):
//   auto def = glow_text(opts, glow_color, radius, intensity);
//   // After (Blocco 5.1 canonical — direct TextDefinition construction):
//   auto def = TextDefinition{
//       .content = {.value = std::move(opts.text)},
//       .style = {
//           .font = {.font_path = std::move(opts.font_asset), .font_size = opts.font_size, .font_weight = opts.font_weight},
//           .color = opts.color,
//           .material = {.use_material_glow = true,
//                        .glow_color        = glow_color,
//                        .glow_radius       = radius,
//                        .glow_intensity    = intensity},
//       },
//       .frame = {.size = opts.box, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle},
//   };
//
/// F2 + Phase A5 — wires glow parameters to TextMaterial.use_material_glow +
/// TextMaterial.glow_{color,radius,intensity} on the returned TextDefinition.
/// `TextMaterial` is the single canonical compositor surface; the prior
/// `TextEffects` duplicate struct was eliminated in Phase A5 close-out.
[[deprecated("Use direct TextDefinition construction + set .style.material.{use_material_glow,glow_*} — "
             "see TICKET-CENTERED-TEXT-MIGRATION (Blocco 5.1). The helper is removed after migration lands.")]]
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
