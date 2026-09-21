#pragma once

// TextRenderResources is the backend-owned aggregator.  The focused font and
// scratch surfaces live in text_font_resources.hpp and text_scratch_pool.hpp;
// this compatibility header keeps the historical public include stable.

#include <blend2d.h>

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/backends/text/text_font_resources.hpp>
#include <chronon3d/backends/text/text_scratch_pool.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace chronon3d {

/// Cached rasterization result for a text run.
struct TextRasterization {
    BLImage image;
};

namespace assets { class AssetResolver; }
namespace detail {
    struct TextRasterCache;
    class GlyphAtlasCache;
}
struct GlyphAtlasEntry;
struct GlyphAtlasStats;
struct PlacedGlyphRun;

// TextRenderResources — aggregator owning font caches, scratch, and the
// per-renderer raster/glyph-atlas caches.
struct TextRenderResources {
    TextRenderResources();
    ~TextRenderResources();

    BLFontFaceCache bl_faces;

#ifdef CHRONON3D_ENABLE_TEXT
    FreeTypeFaceCache ft_faces;
    GlyphOutlineBuilder outlines;
#endif

    TextScratchManager scratch_manager;

    std::atomic<bool> debug_io_fence{false};

    void set_debug_io_fence(bool on) noexcept {
        bind_fences();
        debug_io_fence.store(on, std::memory_order_release);
    }

    [[nodiscard]] bool is_debug_io_fence_active() const noexcept {
        return debug_io_fence.load(std::memory_order_acquire);
    }

    void bind_fences() noexcept {
        bl_faces.set_fence(&debug_io_fence);
#ifdef CHRONON3D_ENABLE_TEXT
        ft_faces.set_fence(&debug_io_fence);
#endif
    }

    [[nodiscard]] FontFaceHandle resolve_handle(
        const std::string& font_path,
        f32 font_size,
        const assets::AssetResolver& resolver
    );

    std::unique_ptr<detail::TextRasterCache> raster_cache;

    void set_raster_cache_capacity(size_t max_bytes);
    void clear_raster_cache();
    std::shared_ptr<TextRasterization> lookup_raster_cache(uint64_t key);
    void store_raster_cache(uint64_t key, std::shared_ptr<TextRasterization> result);

    std::unique_ptr<detail::GlyphAtlasCache> glyph_atlas;

    void set_glyph_atlas_capacity(size_t max_bytes);
    void clear_glyph_atlas();

    std::optional<GlyphAtlasEntry> lookup_glyph_atlas(
        const std::string& font_path,
        u32 glyph_id,
        u32 font_size
    );

    std::optional<GlyphAtlasEntry> lookup_glyph_atlas(
        std::uint64_t font_id,
        u32 glyph_id,
        u32 font_size
    );

    void store_glyph_atlas(
        const std::string& font_path,
        u32 glyph_id,
        u32 font_size,
        const GlyphAtlasEntry& entry
    );

    void store_glyph_atlas_from_placed_run(
        const std::string& font_path,
        const BLImage& rendered_text,
        const PlacedGlyphRun& placed,
        const BLFont& font,
        float origin_x,
        float origin_y,
        float font_size,
        u32 fill_color_rgba
    );

    GlyphAtlasStats glyph_atlas_stats() const;

private:
    void ensure_glyph_atlas_materialized();
    std::once_flag glyph_atlas_init_flag;
};

struct FontPreflightSummary {
    size_t preflight_attempted{0};
    size_t preflight_succeeded{0};
    size_t preflight_missing{0};
    bool ok() const noexcept {
        return preflight_missing == 0 && preflight_succeeded != 0;
    }
};

} // namespace chronon3d
