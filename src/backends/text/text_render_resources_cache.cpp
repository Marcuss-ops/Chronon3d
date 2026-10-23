#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <spdlog/spdlog.h>

namespace chronon3d {

// Per-renderer text raster and glyph atlas cache implementations.

namespace detail {

struct TextRasterCache {
    using CacheKey = u64;
    using CacheValue = std::shared_ptr<TextRasterization>;
    using TextCache = cache::LruCache<CacheKey, CacheValue>;

    /// Default capacity used when the caller passes `0`.
    static constexpr size_t kFallbackBytes = 32ULL * 1024ULL * 1024ULL;

    std::shared_mutex mutex;
    TextCache cache;

    /// Capacity is fixed when the cache is first materialized; later capacity
    /// updates have no effect. The renderer sets it during construction.
    explicit TextRasterCache(size_t capacity_in)
        : cache(capacity_in > 0 ? capacity_in : kFallbackBytes, 8) {}
};

// Glyph atlas state is owned by TextRenderResources. The ABI-frozen
// rasterize_text_to_bl_image path bypasses this per-renderer cache.

struct GlyphAtlasKey {
    std::uint64_t font_id{0};
    u32         glyph_id{0};
    u32         font_size{0};

    bool operator==(const GlyphAtlasKey& o) const noexcept {
        return font_id == o.font_id
            && glyph_id == o.glyph_id
            && font_size == o.font_size;
    }
};

struct GlyphAtlasKeyHash {
    [[nodiscard]] std::size_t operator()(const GlyphAtlasKey& k) const noexcept {
        std::size_t h = static_cast<std::size_t>(k.font_id);
        h ^= std::hash<u32>{}(k.glyph_id) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.font_size) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        return h;
    }
};

struct GlyphAtlasCache {
    using Cache = cache::LruCache<GlyphAtlasKey, GlyphAtlasEntry, GlyphAtlasKeyHash>;

    /// Default capacity used when the caller passes `0`.
    static constexpr size_t kFallbackBytes = 32ULL * 1024ULL * 1024ULL;

    /// Coordinates lookup and store across shards to keep the cache's
    /// lookup-then-insert sequence atomic for this renderer instance.
    mutable std::shared_mutex mutex;
    Cache cache;

    /// Capacity is fixed when the cache is first materialized; later capacity
    /// updates have no effect. The renderer sets it during construction.
    explicit GlyphAtlasCache(size_t capacity_in)
        : cache(capacity_in > 0 ? capacity_in : kFallbackBytes, 8) {}
};

} // namespace detail

// Raster cache capacity and per-instance cache operations.

void TextRenderResources::set_raster_cache_capacity(size_t max_bytes) {
    if (!raster_cache) {
        raster_cache = std::make_unique<detail::TextRasterCache>(max_bytes);
    }
    // Capacity is fixed when the cache is first materialized.
}




// ═══════════════════════════════════════════════════════════════════════════
// Glyph atlas capacity and per-instance cache operations.

void TextRenderResources::set_glyph_atlas_capacity(size_t max_bytes) {
    if (!glyph_atlas) {
        glyph_atlas = std::make_unique<detail::GlyphAtlasCache>(max_bytes);
    }
    // Capacity is fixed when the cache is first materialized.
}

void TextRenderResources::ensure_glyph_atlas_materialized() {
    // Materialize on first use so lookup and store are safe even when the
    // renderer did not set an explicit capacity. call_once protects concurrent
    // access and gives each TextRenderResources its own cache.
    std::call_once(glyph_atlas_init_flag, [this]() {
        if (!glyph_atlas) {
            glyph_atlas = std::make_unique<detail::GlyphAtlasCache>(0);
        }
    });
}

void TextRenderResources::clear_glyph_atlas() {
    ensure_glyph_atlas_materialized();
    std::unique_lock lock(glyph_atlas->mutex);
    glyph_atlas->cache.clear();
}

std::optional<GlyphAtlasEntry> TextRenderResources::lookup_glyph_atlas(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size
) {
    return lookup_glyph_atlas(glyph_font_id(font_path), glyph_id, font_size);
}

std::optional<GlyphAtlasEntry> TextRenderResources::lookup_glyph_atlas(
    std::uint64_t font_id,
    u32 glyph_id,
    u32 font_size
) {
    // TICKET-TEXT-TIMING-V1 — time the per-glyph atlas lookup so a cache
    // probe that regressed into re-rasterization is visible in telemetry.
    const bool timing_enabled = profiling::g_current_counters != nullptr;
    const auto lookup_t0 = timing_enabled
        ? profiling::now() : profiling::Clock::time_point{};
    ensure_glyph_atlas_materialized();
    detail::GlyphAtlasKey key{font_id, glyph_id, font_size};
    std::shared_lock lock(glyph_atlas->mutex);
    auto result = glyph_atlas->cache.get(key);
    if (timing_enabled) {
        profiling::g_current_counters->glyph_cache_lookup_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(lookup_t0))),
            std::memory_order_relaxed);
        if (result.has_value()) {
            profiling::g_current_counters->glyph_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            profiling::g_current_counters->glyph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return result;
}

void TextRenderResources::store_glyph_atlas(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size,
    const GlyphAtlasEntry& entry
) {
    // TICKET-TEXT-TIMING-V1 — time the atlas upload/store so glyph-atlas
    // population cost (frame 0 vs steady-state 0) is visible in telemetry.
    const bool timing_enabled = profiling::g_current_counters != nullptr;
    const auto store_t0 = timing_enabled
        ? profiling::now() : profiling::Clock::time_point{};
    ensure_glyph_atlas_materialized();

    if (!entry.image || entry.image->empty()) {
        spdlog::error(
            "[text-atlas] refusing invalid glyph: font='{}' glyph={} size={}",
            font_path, glyph_id, font_size);
        return;
    }

    const auto w = static_cast<std::size_t>(entry.image->width());
    const auto h = static_cast<std::size_t>(entry.image->height());

    if (w == 0 || h == 0 ||
        w > SIZE_MAX / 4 ||
        h > SIZE_MAX / (w * 4)) {
        spdlog::error(
            "[text-atlas] invalid glyph dimensions {}x{}", w, h);
        return;
    }

    // Weight is the image byte size (width × height × 4 for PRGB32);
    // the metadata struct is ~24 bytes and is amortized over the image
    // bytes — counting it would distort cache pressure for negligible
    // gain.  The shared_ptr<BLImage> inside the entry gets its ref-count
    // incremented (cheap, atomic) so the cache owns the new instance
    // until eviction.
    const std::size_t weight = w * h * 4;
    detail::GlyphAtlasKey key{glyph_font_id(font_path), glyph_id, font_size};
    std::unique_lock lock(glyph_atlas->mutex);
    glyph_atlas->cache.put(key, entry, weight);
    if (timing_enabled) {
        profiling::g_current_counters->glyph_atlas_upload_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(store_t0))),
            std::memory_order_relaxed);
        profiling::g_current_counters->glyph_atlas_stored.fetch_add(1, std::memory_order_relaxed);
    }
}

void TextRenderResources::store_glyph_atlas_from_placed_run(
    const std::string& font_path,
    const BLImage& rendered_text,
    const PlacedGlyphRun& placed,
    const BLFont& font,
    float origin_x,
    float origin_y,
    float font_size,
    u32 fill_color_rgba
) {
    ensure_glyph_atlas_materialized();
    BLImageData img_data;
    const auto data_status = rendered_text.getData(&img_data);
    if (data_status != BL_SUCCESS) return;
    if (!img_data.pixelData || img_data.size.w <= 0 || img_data.size.h <= 0) return;

    const int img_w = img_data.size.w;
    const int img_h = img_data.size.h;
    const int stride = static_cast<int>(img_data.stride / sizeof(uint32_t));
    const auto* src = static_cast<const uint32_t*>(img_data.pixelData);
    const u32 fs = static_cast<u32>(std::ceil(font_size));

    for (const auto& pg : placed.glyphs) {
        if (pg.glyph_id == 0) continue;

        auto existing = lookup_glyph_atlas(font_path, pg.glyph_id, fs);
        if (existing && existing->fill_color_rgba == fill_color_rgba) continue;

        BLBoxI bbox_i;
        const u32 gid = pg.glyph_id;
        const auto bounds_status = font.getGlyphBounds(&gid, intptr_t{0}, &bbox_i, 1);
        if (bounds_status != BL_SUCCESS) {
            continue;
        }

        // Blend2D returns glyph bounds in 26.6 fixed-point units.
        constexpr float kGlyphUnit = 1.0f / 64.0f;
        const float bx0 = static_cast<float>(bbox_i.x0) * kGlyphUnit;
        const float by0 = static_cast<float>(bbox_i.y0) * kGlyphUnit;
        const float bx1 = static_cast<float>(bbox_i.x1) * kGlyphUnit;
        const float by1 = static_cast<float>(bbox_i.y1) * kGlyphUnit;
        const int gx = static_cast<int>(std::floor(origin_x + pg.x + bx0));
        const int gy = static_cast<int>(std::floor(origin_y + pg.y + by0));
        const int gw = static_cast<int>(std::ceil(bx1 - bx0));
        const int gh = static_cast<int>(std::ceil(by1 - by0));
        if (gw <= 0 || gh <= 0) continue;
        if (gx < 0 || gy < 0 || gx + gw > img_w || gy + gh > img_h) continue;

        auto glyph_img = std::make_shared<BLImage>(gw, gh, BL_FORMAT_PRGB32);
        {
            BLImageData glyph_data;
            if (glyph_img->getData(&glyph_data) != BL_SUCCESS) continue;
            auto* dst = static_cast<uint32_t*>(glyph_data.pixelData);
            const int gs = static_cast<int>(glyph_data.stride / sizeof(uint32_t));
            for (int y = 0; y < gh; ++y)
                for (int x = 0; x < gw; ++x)
                    dst[y * gs + x] = src[(gy + y) * stride + (gx + x)];
        }

        GlyphAtlasEntry entry;
        entry.image            = glyph_img;
        entry.x_offset         = static_cast<int>(std::lround(bx0));
        entry.y_offset         = static_cast<int>(std::lround(by0));
        entry.advance_x        = pg.advance_x;
        entry.fill_color_rgba  = fill_color_rgba;
        store_glyph_atlas(font_path, pg.glyph_id, fs, entry);

        if (profiling::g_current_counters) {
            profiling::g_current_counters->glyph_atlas_stored.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

GlyphAtlasStats TextRenderResources::glyph_atlas_stats() const {
    GlyphAtlasStats s;
    if (!glyph_atlas) return s;
    std::shared_lock lock(glyph_atlas->mutex);
    auto st = glyph_atlas->cache.stats();
    s.entry_count  = st.current_size;
    s.total_weight = st.current_weight;
    s.hits         = st.hits;
    s.misses       = st.misses;
    return s;
}
// PIMPL'd ctor/dtor — defined here where detail::TextRasterCache is complete.
TextRenderResources::TextRenderResources() = default;
TextRenderResources::~TextRenderResources() = default;

} // namespace chronon3d
