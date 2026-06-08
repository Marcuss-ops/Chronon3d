#pragma once

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

// ── GlyphAtlas — per-glyph rasterization cache ─────────────────────────
//
// Caches individual glyph bitmaps keyed by (font_path, glyph_id, font_size).
// Enables reuse of glyph images across different text strings using the same
// font, reducing redundant rasterization and shaping for repeated glyphs.
//
// Thread-safe via internal std::shared_mutex.

struct GlyphAtlasEntry {
    std::shared_ptr<BLImage> image;
    int    x_offset{0};  // offset from origin to glyph left edge
    int    y_offset{0};
    float  advance_x{0.0f};
};

// Returns a cached glyph entry or std::nullopt on miss.
[[nodiscard]] std::optional<GlyphAtlasEntry> glyph_atlas_lookup(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size
);

// Stores a glyph in the atlas. Weight is image width × height × 4 bytes.
void glyph_atlas_store(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size,
    const GlyphAtlasEntry& entry
);

// Clears the glyph atlas.
void glyph_atlas_clear();

// Returns current stats: (entry_count, total_weight_bytes, hits, misses).
struct GlyphAtlasStats {
    size_t entry_count{0};
    size_t total_weight{0};
    size_t hits{0};
    size_t misses{0};
};
[[nodiscard]] GlyphAtlasStats glyph_atlas_stats();

// Extracts individual glyph bitmaps from a rendered text image and stores
// them in the atlas.  Uses the glyph buffer (shaped by BLFont) to locate
// each glyph's position and bounding box in the rendered image.
void glyph_atlas_store_from_text(
    const std::string& font_path,
    const BLImage& rendered_text,
    const BLGlyphBuffer& gb,
    const BLFont& font,
    float text_origin_x,
    float text_origin_y,
    float font_size
);

} // namespace chronon3d
