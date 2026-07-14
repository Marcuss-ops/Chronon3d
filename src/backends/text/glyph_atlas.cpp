#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <blend2d.h>

#include <string>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// P1-9 — GlyphAtlas free functions are now THIN WRAPPERS around
// `TextRenderResources` member functions.  The static state (cache,
// capacity, atomic first-call-wins, shared_mutex) and the 4 global
// accessors (`set_glyph_atlas_capacity`, `get_glyph_atlas`,
// `get_glyph_atlas_mutex`, `glyph_atlas_clear`) are GONE — the atlas
// now lives per-instance on `TextRenderResources::glyph_atlas`.
// See `text_render_resources.hpp` for the canonical surface.
// ═══════════════════════════════════════════════════════════════════════════

std::optional<GlyphAtlasEntry> glyph_atlas_lookup(
    TextRenderResources& res,
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size
) {
    return res.lookup_glyph_atlas(font_path, glyph_id, font_size);
}

void glyph_atlas_store(
    TextRenderResources& res,
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size,
    const GlyphAtlasEntry& entry
) {
    res.store_glyph_atlas(font_path, glyph_id, font_size, entry);
}

GlyphAtlasStats glyph_atlas_stats(const TextRenderResources& res) {
    return res.glyph_atlas_stats();
}

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
) {
    res.store_glyph_atlas_from_placed_run(
        font_path, rendered_text, placed, font,
        origin_x, origin_y, font_size, fill_color_rgba);
}

} // namespace chronon3d
