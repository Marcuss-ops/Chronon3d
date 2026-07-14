#pragma once

#include <chronon3d/backends/text/text_render_resources.hpp>  // P1-9: TextRenderResources owner
#include <chronon3d/core/types/types.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
// (per-renderer ownership).  The 4 free functions below are THIN WRAPPERS
// that delegate to the `TextRenderResources` member functions, taking
// `TextRenderResources&` as their FIRST parameter.
//
// The 4 deleted globals (set_glyph_atlas_capacity, get_glyph_atlas,
// get_glyph_atlas_mutex, glyph_atlas_clear) are GONE.  Callers must use:
//   res.set_glyph_atlas_capacity(N)   // (was set_glyph_atlas_capacity)
//   res.clear_glyph_atlas()           // (was glyph_atlas_clear)
//   res.lookup_glyph_atlas(...)       // (was glyph_atlas_lookup)
//   res.store_glyph_atlas(...)        // (was glyph_atlas_store)
//   res.store_glyph_atlas_from_placed_run(...)  // (was glyph_atlas_store_from_placed_run)
//   res.glyph_atlas_stats()           // (was glyph_atlas_stats)

struct GlyphAtlasEntry {
    std::shared_ptr<BLImage> image;
    int    x_offset{0};  // pen-relative left offset (bbox.x0)
    int    y_offset{0};  // pen-relative top  offset (bbox.y0)
    float  advance_x{0.0f};
    u32    fill_color_rgba{0};  // solid fill color used at rasterization time
};

// Returns a cached glyph entry or std::nullopt on miss.
[[nodiscard]] std::optional<GlyphAtlasEntry> glyph_atlas_lookup(
    TextRenderResources& res,
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size
);

// Stores a glyph in the atlas. Weight is image width × height × 4 bytes.
void glyph_atlas_store(
    TextRenderResources& res,
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size,
    const GlyphAtlasEntry& entry
);

// Returns current stats: (entry_count, total_weight_bytes, hits, misses).
struct GlyphAtlasStats {
    size_t entry_count{0};
    size_t total_weight{0};
    size_t hits{0};
    size_t misses{0};
};
[[nodiscard]] GlyphAtlasStats glyph_atlas_stats(
    const TextRenderResources& res
);

// Store individual glyph bitmaps from a HarfBuzz-shaped PlacedGlyphRun.
// Uses pg.x/pg.y + font.getGlyphBounds() to locate each glyph in the
// rendered image.  Skips glyphs already cached with the same fill_color_rgba.
void glyph_atlas_store_from_placed_run(
    TextRenderResources& res,
    const std::string& font_path,
    const BLImage& rendered_text,
    const PlacedGlyphRun& placed,
    const BLFont& font,
    float origin_x,
    float origin_y,
    float font_size,
    u32 fill_color_rgba
);

} // namespace chronon3d
