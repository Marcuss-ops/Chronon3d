// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// FASE 3 Step 1 — GlyphAtlas helpers extracted from text_rasterizer_render.cpp
//
// Contains:
//   - PendingGlyphStore: deferred glyph storage for atlas caching
//   - can_use_glyph_atlas(): predicate for atlas eligibility
//   - resolve_atlas_fill_rgba(): extracts fill RGBA for atlas keying
//   - try_atlas_blit(): fast-path blit from cached glyph images
//   - store_pending_glyphs(): post-render store loop
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include "../software/utils/blend2d_paint.hpp"
#include <blend2d.h>

#include <string>
#include <vector>

namespace chronon3d {
namespace detail {

/// Deferred glyph storage entry — collected during rendering, stored to the
/// glyph atlas after ctx.end() so we don't extract glyphs from a live context.
struct PendingGlyphStore {
    std::string    font_path;
    PlacedGlyphRun placed;
    BLFont         font;
    float          origin_x{0.0f}, origin_y{0.0f};
    float          font_size{0.0f};
    u32            fill_rgba{0};
};

/// Returns true when the glyph atlas can be used for this run.
/// Conditions: not a geometric transform, no box background, no stroke,
/// fill is solid-color.
inline bool can_use_glyph_atlas(
    bool use_geometric_transform,
    bool box_style_enabled,
    bool has_stroke,
    const std::optional<Fill>& fill_style)
{
    return !use_geometric_transform
        && !box_style_enabled
        && !has_stroke
        && (!fill_style.has_value()
            || fill_style->type == FillType::Solid);
}

/// Resolve the fill RGBA for atlas keying.  Prefers the solid fill
/// from fill_style; falls back to the resolved Color.
inline u32 resolve_atlas_fill_rgba(
    const std::optional<Fill>& fill_style,
    u32 fallback_rgba)
{
    if (fill_style.has_value()
        && fill_style->type == FillType::Solid)
        return blend2d_bridge::paint::to_bl_rgba(
            fill_style->solid).value;
    return fallback_rgba;
}

/// Try to blit all glyphs from the atlas cache.  Returns false on the
/// first miss (caller falls back to fillGlyphRun + pending store).
inline bool try_atlas_blit(
    BLContext& ctx,
    const PlacedGlyphRun& run,
    const std::string& font_path,
    float font_size,
    u32 fill_rgba,
    float origin_x,
    float origin_y)
{
    if (run.glyphs.empty()) return false;
    const u32 fsu = static_cast<u32>(std::ceil(font_size));
    std::vector<GlyphAtlasEntry> entries;
    entries.reserve(run.glyphs.size());
    for (const auto& pg : run.glyphs) {
        auto e = glyph_atlas_lookup(font_path, pg.glyph_id, fsu);
        if (!e || e->fill_color_rgba != fill_rgba) return false;
        entries.push_back(std::move(*e));
    }
    for (size_t i = 0; i < run.glyphs.size(); ++i) {
        const auto& pg = run.glyphs[i];
        const auto& e  = entries[i];
        ctx.blitImage(
            BLPoint(origin_x + pg.x + static_cast<float>(e.x_offset),
                    origin_y + pg.y + static_cast<float>(e.y_offset)),
            *e.image);
    }
    return true;
}

/// Post-render: store collected glyphs into the atlas for future reuse.
inline void store_pending_glyphs(
    const std::vector<PendingGlyphStore>& pending,
    const BLImage& rendered_image)
{
    for (const auto& ps : pending) {
        glyph_atlas_store_from_placed_run(
            ps.font_path, rendered_image, ps.placed, ps.font,
            ps.origin_x, ps.origin_y, ps.font_size, ps.fill_rgba);
    }
}

} // namespace detail
} // namespace chronon3d
