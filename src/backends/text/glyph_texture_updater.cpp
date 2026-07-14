// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/glyph_texture_updater.cpp
//
// FASE 4 — Implementation extracted from text_rasterizer_render.cpp.
// See `glyph_texture_updater.hpp` for the design contract and motivation.
// ═══════════════════════════════════════════════════════════════════════════

#include "glyph_texture_updater.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include "blend2d_glyph_conversion.hpp"          // HbToBlGlyphRun::from
#include "text_rasterizer_atlas.hpp"             // detail::{can_use_glyph_atlas, try_atlas_blit}

namespace chronon3d {

GlyphTextureUpdater::Path GlyphTextureUpdater::render_placed(
    BLContext&                                ctx,
    TextRenderResources*                      res,
    const std::optional<PlacedGlyphRun>&      placed_opt,
    const BLFontFace&                         face,
    const BLFont&                             font,
    float                                     origin_x,
    float                                     origin_y,
    const std::string&                        font_path,
    float                                     font_size,
    u32                                       fill_rgba,
    bool                                      use_geometric_transform,
    bool                                      box_style_enabled,
    bool                                      has_stroke,
    const std::optional<Fill>&                fill_style
) {
    using Path = GlyphTextureUpdater::Path;

    if (!placed_opt) {
        // No shaped glyphs: caller falls back to ctx.fillUtf8Text using
        // its own font+text refs.  Helper stays out of that path —
        // facing and run.text references vary across the two callers.
        return Path::NoPlaced;
    }

    if (!res || !detail::can_use_glyph_atlas(use_geometric_transform,
                                    box_style_enabled,
                                    has_stroke,
                                    fill_style)) {
        // Atlas NOT eligible (transform/box/stroke/gradient constraint):
        // paint glyphs via HbToBlGlyphRun directly.  No atlas entry scheduled.
        auto bl = HbToBlGlyphRun::from(*placed_opt, face, font_size);
        ctx.fillGlyphRun(BLPoint(origin_x, origin_y), font, bl.bl_run);
        return Path::Miss;
    }

    if (detail::try_atlas_blit(ctx, res, *placed_opt, font_path, font_size,
                               fill_rgba, origin_x, origin_y)) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->glyph_atlas_hits
                .fetch_add(1, std::memory_order_relaxed);
        }
        return Path::Hit;
    }

    // Atlas miss — direct fillGlyphRun issued; caller is responsible
    // for std::move()'ing the placed run into a PendingGlyphStore for
    // post-render atlas storage (caller's choice — sometimes skipped,
    // e.g. for ephemeral debug runs).
    auto bl = HbToBlGlyphRun::from(*placed_opt, face, font_size);
    ctx.fillGlyphRun(BLPoint(origin_x, origin_y), font, bl.bl_run);
    if (profiling::g_current_counters) {
        profiling::g_current_counters->glyph_atlas_misses
            .fetch_add(1, std::memory_order_relaxed);
    }
    return Path::Miss;
}

} // namespace chronon3d
