#pragma once

#include <chronon3d/backends/text/text_render_resources.hpp>  // P1-9: forward-decls GlyphAtlasEntry, GlyphAtlasStats
#include <chronon3d/core/types/types.hpp>

#include <memory>

// Forward declarations for Blend2D types (defined in <blend2d.h>)
class BLGlyphBuffer;
class BLFont;
class BLImage;
class BLFontFace;

namespace chronon3d {

struct PlacedGlyphRun;  // forward-declare (font_engine.hpp)

// ── GlyphAtlas — per-glyph rasterization cache ─────────────────────────
//
// Caches individual glyph bitmaps keyed by (font_path, glyph_id, font_size).
// Enables reuse of glyph images across different text strings using the same
// font, reducing redundant rasterization and shaping for repeated glyphs.
//
// P1-9 migration: the cache state, capacity, and external shared_mutex
// are GONE from this TU.  The atlas now lives on `TextRenderResources`
// (per-renderer ownership).  The 4 free functions that used to live
// here (`glyph_atlas_lookup`, `glyph_atlas_store`, `glyph_atlas_stats`,
// `glyph_atlas_store_from_placed_run`) are DELETED — they were thin
// wrappers around the `TextRenderResources` member functions and added
// no value (Cat-3 anti-duplication).  Callers now use member methods
// directly:
//
//   res.set_glyph_atlas_capacity(N)   // (was set_glyph_atlas_capacity)
//   res.clear_glyph_atlas()           // (was glyph_atlas_clear)
//   res.lookup_glyph_atlas(...)       // (was glyph_atlas_lookup)
//   res.store_glyph_atlas(...)        // (was glyph_atlas_store)
//   res.store_glyph_atlas_from_placed_run(...)  // (was glyph_atlas_store_from_placed_run)
//   res.glyph_atlas_stats()           // (was glyph_atlas_stats)
//
// This header now contains ONLY the value types (GlyphAtlasEntry,
// GlyphAtlasStats) so existing includes keep working.  The full
// canonical surface lives on `TextRenderResources`.

struct GlyphAtlasEntry {
    std::shared_ptr<BLImage> image;
    int    x_offset{0};  // pen-relative left offset (bbox.x0)
    int    y_offset{0};  // pen-relative top  offset (bbox.y0)
    float  advance_x{0.0f};
    u32    fill_color_rgba{0};  // solid fill color used at rasterization time
};

// Returns current stats: (entry_count, total_weight_bytes, hits, misses).
struct GlyphAtlasStats {
    size_t entry_count{0};
    size_t total_weight{0};
    size_t hits{0};
    size_t misses{0};
};

} // namespace chronon3d
