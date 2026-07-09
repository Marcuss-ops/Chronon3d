// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/glyph_texture_updater.hpp
//
// FASE 4 — GlyphTextureUpdater extracted from text_rasterizer_render.cpp.
//
// Internal-only header: lives in src/backends/text/, NOT in include/chronon3d/.
// Centralizes the atlas-or-fill placed-glyph rendering pipeline that was
// duplicated in BOTH `render_run` lambda AND `for-line` single-run block of
// the legacy `rasterize_text_to_bl_image` function.
//
// Pure conversion utility (AGENTS.md Cat-5 ABI-stable):
//   * NO caching logic added.  Atlas lookup uses the existing
//     `text_rasterizer_atlas.hpp::{can_use_glyph_atlas, try_atlas_blit,
//     resolve_atlas_fill_rgba}`.  Subsequent post-render storage uses
//     the existing `text_rasterizer_atlas.hpp::store_pending_glyphs`.
//   * NO font-face I/O.  Faces are supplied by the caller.
//   * NO own state.  `render_placed` is a STATIC method and the class
//     itself is not instantiable.
//
// Anti-duplication invariant:
//   This helper does NOT replicate any cache.  It only routes the
//   placed-glyph render into:
//     (1) atlas blit (when can_use_glyph_atlas + try_atlas_blit Hit),
//     (2) direct HbToBlGlyphRun fillGlyphRun (eligibility-fail path or
//         atlas-miss fallback — caller schedules the PendingGlyphStore),
//     (3) the caller's fillUtf8Text fallback (when no placed run).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <optional>
#include <string>

#include <blend2d.h>

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>

namespace chronon3d {

/// Atlas-or-fill placed-glyph renderer.  Stateless utility whose single
/// static method centralizes the route-atlas / route-direct / route-fallback
/// decision that used to live inlined in `text_rasterizer_render.cpp` (twice).
///
/// Lives in the same internal-only namespace as the rest of
/// `src/backends/text/` — does NOT expand the public ABI surface
/// (AGENTS.md Cat-5 ABI-stable compliant).
class GlyphTextureUpdater {
public:
    /// Reports which rendering path `render_placed` chose for the run.
    enum class Path {
        /// Pre-existing atlas entry was used for the whole run.  No
        /// pending store scheduled.  Caller does NOT subsequently call
        /// `fillUtf8Text` — atlas already painted the glyphs.
        Hit,
        /// Atlas miss: direct `fillGlyphRun` was issued and the caller
        /// should std::move() a `PendingGlyphStore` for post-render
        /// atlas storage (out of scope here so the caller retains the
        /// choice to skip atlas-storing some runs).
        Miss,
        /// No shaped placed run available (caller should fall back to
        /// `fillUtf8Text` for Blend2D's internal shaper).
        NoPlaced,
    };

    /// Centralized atlas-or-fill render pipeline for a single placed run.
    ///
    /// `frgba` is the cache key for the atlas lookup; compute it via
    /// `detail::resolve_atlas_fill_rgba(fill_style, fallback_rgba)`.
    /// `placed_opt` is the caller-managed optional `PlacedGlyphRun` —
    /// the helper does NOT consume the placed; the caller decides
    /// whether to `std::move(*placed)` into a pushed PendingGlyphStore
    /// after the helper returns `Path::Miss`.
    ///
    /// Updates `profiling::g_current_counters->glyph_atlas_hits/misses`
    /// consistent with the previous inlined logic.
    static Path render_placed(
        BLContext&                                ctx,
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
    );

private:
    GlyphTextureUpdater() = delete;
};

} // namespace chronon3d
