// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/blend2d_glyph_conversion.cpp
//
// M1.5#11 — implementation of the reusable Blend2D text-glyph conversion
// utility module.  Bodies of `apply_text_style` + its two thin wrappers +
// `HbToBlGlyphRun::from(...)` were moved verbatim out of the legacy
// `text_rasterizer_render.cpp` (which previously owned them in an
// anonymous namespace).
//
// Anti-duplication invariant (M1.5#6 + M1.5#7 constraint):
//   * This module is pure conversion — NO caching logic.
//   * Font-face I/O belongs to M1.5#7's `BLFontFaceCache` (per-session).
//   * The local `Blend2DResources` cache IN text_rasterizer_render.cpp
//     is a legacy standalone dup (deleted with M1.5#9) — not exported
//     here.
//
// Thread-safety: the underlying `apply_text_style` is stateless (no
// caching, no synchronization); `HbToBlGlyphRun` builds an owned-buffer
// struct in the caller's stack.  Caller is responsible for any shared
// `BLContext` synchronization if rendering concurrently.
//
// Compile-time guards: `using chronon3d::blend2d_bridge::paint::to_bl_rgba`
// / `build_bl_gradient` scoped inside `namespace chronon3d { namespace { ... } }`
// so unqualified callers in this TU find them through `chronon3d::to_bl_rgba`
// lookup at the right scope (matches `text_processor_helpers.hpp` pattern).
//
// ═══════════════════════════════════════════════════════════════════════════

#include "blend2d_glyph_conversion.hpp"          // M1.5#11 — same-dir include

#include "../software/utils/blend2d_paint.hpp"   // to_bl_rgba + build_bl_gradient + StyleOp

#include <blend2d.h>

namespace chronon3d {

// Using-declarations inside `namespace chronon3d` so unqualified callers
// in this TU find them through `chronon3d::to_bl_rgba` lookup at the
// right scope (consistent with `text_processor_helpers.hpp`).
using chronon3d::blend2d_bridge::paint::to_bl_rgba;
using chronon3d::blend2d_bridge::paint::build_bl_gradient;

// ── Public surface ────────────────────────────────────────────────────────

void apply_text_style(
    BLContext&                                   ctx,
    chronon3d::blend2d_bridge::paint::StyleOp    op,
    const std::optional<Fill>&                   style_opt,
    const Color&                                 fallback_color,
    float                                        origin_x,
    float                                        origin_y,
    float                                        width,
    float                                        height
) {
    // Unified BLContext fill/stroke style applicator — `op` discriminates
    // `setFillStyle` vs `setStrokeStyle` so the legacy duplication between
    // `apply_text_fill_style` and `apply_text_stroke_style` (textual
    // mirror images) collapses to one body.
    auto set = [&](auto&& value) {
        if (op == chronon3d::blend2d_bridge::paint::StyleOp::Fill)
            ctx.setFillStyle(std::forward<decltype(value)>(value));
        else
            ctx.setStrokeStyle(std::forward<decltype(value)>(value));
    };

    if (!style_opt.has_value()) {
        set(to_bl_rgba(fallback_color));
        return;
    }
    const Fill& fill = *style_opt;
    if (fill.type == FillType::Solid) {
        set(to_bl_rgba(fill.solid));
        return;
    }
    if (auto gradient = build_bl_gradient(fill, origin_x, origin_y, width, height)) {
        set(*gradient);
        return;
    }
    set(to_bl_rgba(fallback_color));
}

void apply_text_fill_style(
    BLContext&           ctx,
    const TextStyle&     style,
    const Color&         fallback_color,
    float                origin_x,
    float                origin_y,
    float                width,
    float                height
) {
    apply_text_style(
        ctx,
        chronon3d::blend2d_bridge::paint::StyleOp::Fill,
        style.paint.fill_style, fallback_color,
        origin_x, origin_y, width, height
    );
}

void apply_text_stroke_style(
    BLContext&           ctx,
    const TextStyle&     style,
    const Color&         fallback_stroke_color,
    float                origin_x,
    float                origin_y,
    float                width,
    float                height
) {
    apply_text_style(
        ctx,
        chronon3d::blend2d_bridge::paint::StyleOp::Stroke,
        style.paint.stroke_style, fallback_stroke_color,
        origin_x, origin_y, width, height
    );
}

// ── HbToBlGlyphRun — HarfBuzz placed-glyph → Blend2D BLGlyphRun converter ─

HbToBlGlyphRun HbToBlGlyphRun::from(
    const PlacedGlyphRun&   placed,
    const BLFontFace&       face,
    float                   font_size
) {
    // Convert pixel values to font design units (Blend2D's
    // ADVANCE_OFFSET expects design units, not pixels).
    const double upem = static_cast<double>(face.unitsPerEm());
    const double scale = (upem > 0.0 && font_size > 0.0f)
        ? upem / static_cast<double>(font_size) : 1.0;

    HbToBlGlyphRun result;
    result.glyph_ids.reserve(placed.glyphs.size());
    result.placements.reserve(placed.glyphs.size());
    for (const auto& pg : placed.glyphs) {
        result.glyph_ids.push_back(pg.glyph_id);
        BLGlyphPlacement p;
        // Use raw offsets (not cumulative x/y), matching
        // BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET semantics.
        p.placement.reset(pg.x_offset * scale, pg.y_offset * scale);
        p.advance.reset(pg.advance_x * scale, pg.advance_y * scale);
        result.placements.push_back(p);
    }
    result.bl_run.glyphData = result.glyph_ids.data();
    result.bl_run.glyphAdvance = int8_t(sizeof(uint32_t));
    result.bl_run.placementData = result.placements.data();
    result.bl_run.placementAdvance = int8_t(sizeof(BLGlyphPlacement));
    result.bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
    result.bl_run.size = result.glyph_ids.size();
    return result;
}

} // namespace chronon3d
