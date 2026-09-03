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

// ═══════════════════════════════════════════════════════════════════════════
// P1-8 — TextRasterCache PIMPL + `hash_text_style` impl + raster cache API
// ═══════════════════════════════════════════════════════════════════════════
//
// The cache state that USED to live in the retired
// `src/backends/text/text_rasterizer_cache.cpp`  // drift-class: historical (file retired; state co-located here)
// is now co-located with TextRenderResources.  The 4 free functions
// (`set/lookup/store_text_cache` + `clear_text_raster_cache`) are deleted.
// Production callers access via the TextRenderResources members declared
// in `text_render_resources.hpp`.  The legacy `rasterize_text_to_bl_image`
// ABI-frozen TU bypasses the cache (Cat-5 ABI stability constraint); the
// per-renderer cache is used by non-legacy renderer paths.

namespace detail {

struct TextRasterCache {
    using CacheKey = u64;
    using CacheValue = std::shared_ptr<TextRasterization>;
    using TextCache = cache::LruCache<CacheKey, CacheValue>;

    /// 32 MiB fallback — matches the legacy
    /// `text_rasterizer_cache.cpp::kFallback` (TICKET-079).  Used when a
    /// caller passes `0` to the ctor.
    static constexpr size_t kFallbackBytes = 32ULL * 1024ULL * 1024ULL;

    std::shared_mutex mutex;
    TextCache cache;

    /// Ctor MATERIALIZES the cache eagerly with the requested capacity
    /// (or fallback if 0).  Eager-init mirrors the legacy first-call-wins
    /// semantics in `text_rasterizer_cache.cpp::resolve_text_cache_capacity()`:
    /// capacity is locked at materialization; post-init capacity updates
    /// are silently ignored.  The renderer MUST call
    /// `TextRenderResources::set_raster_cache_capacity(N)` from its ctor
    /// BEFORE the first cache touch for the injected capacity to apply.
    explicit TextRasterCache(size_t capacity_in)
        : cache(capacity_in > 0 ? capacity_in : kFallbackBytes, 8) {}
};

// ── P1-9 — GlyphAtlasCache PIMPL ─────────────────────────────────────
//
// The cache state that USED to live in `src/backends/text/glyph_atlas.cpp`
// is now co-located with TextRenderResources.  The 4 free functions
// (`set/get_glyph_atlas` + `get_glyph_atlas_mutex` + `glyph_atlas_clear`)
// are deleted.  Production callers access via the TextRenderResources
// members declared in `text_render_resources.hpp`.  The legacy
// `rasterize_text_to_bl_image` ABI-frozen TU bypasses the atlas (Cat-5
// ABI stability constraint, matches the P1-8 raster cache bypass); the
// per-renderer atlas is used by non-legacy paths.

struct GlyphAtlasKey {
    std::string font_path;
    u32         glyph_id{0};
    u32         font_size{0};

    bool operator==(const GlyphAtlasKey& o) const noexcept {
        return font_path == o.font_path
            && glyph_id == o.glyph_id
            && font_size == o.font_size;
    }
};

struct GlyphAtlasKeyHash {
    [[nodiscard]] std::size_t operator()(const GlyphAtlasKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.font_path);
        h ^= std::hash<u32>{}(k.glyph_id) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.font_size) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        return h;
    }
};

struct GlyphAtlasCache {
    using Cache = cache::LruCache<GlyphAtlasKey, GlyphAtlasEntry, GlyphAtlasKeyHash>;

    /// 32 MiB fallback — matches the legacy
    /// `glyph_atlas.cpp::kFallback` (TICKET-079).  Used when a caller
    /// passes `0` to the ctor.
    static constexpr size_t kFallbackBytes = 32ULL * 1024ULL * 1024ULL;

    /// External shared_mutex: coordinates cross-shard put/lookup atomicity
    /// (the LruCache's per-shard mutexes protect per-shard access, but
    /// the put+get independent operations need cross-shard coordination
    /// to prevent the "lookup → on-miss-store" test-and-set race).  The
    /// legacy `get_glyph_atlas_mutex()` accessor was REMOVED; callers
    /// that needed cross-instance synchronization no longer exist (the
    /// atlas is now per-instance, owned by ONE TextRenderResources).
    mutable std::shared_mutex mutex;
    Cache cache;

    /// Ctor MATERIALIZES the cache eagerly with the requested capacity
    /// (or fallback if 0).  Eager-init mirrors the legacy first-call-wins
    /// semantics in `glyph_atlas.cpp::resolve_atlas_max_bytes()`: capacity
    /// is locked at materialization; post-init capacity updates are
    /// silently ignored.  The renderer MUST call
    /// `TextRenderResources::set_glyph_atlas_capacity(N)` from its ctor
    /// BEFORE the first cache touch for the injected capacity to apply.
    explicit GlyphAtlasCache(size_t capacity_in)
        : cache(capacity_in > 0 ? capacity_in : kFallbackBytes, 8) {}
};

} // namespace detail

// Per-instance cache API on TextRenderResources.  `raster_cache` is
// allocated lazily on the first `set_raster_cache_capacity` call (called
// by SoftwareRenderer ctor).  Subsequent lookups/store/clear no-op on an
// un-materialized instance.

void TextRenderResources::set_raster_cache_capacity(size_t max_bytes) {
    if (!raster_cache) {
        raster_cache = std::make_unique<detail::TextRasterCache>(max_bytes);
    }
    // Post-materialization calls are silently ignored — see TextRasterCache
    // ctor comment for the rationale (legacy first-call-wins semantics).
}

void TextRenderResources::clear_raster_cache() {
    if (!raster_cache) return;
    std::unique_lock lock(raster_cache->mutex);
    raster_cache->cache.clear();
}

std::shared_ptr<TextRasterization> TextRenderResources::lookup_raster_cache(uint64_t key) {
    if (!raster_cache) return nullptr;
    std::shared_lock lock(raster_cache->mutex);
    auto cached = raster_cache->cache.get(key);
    return cached ? *cached : nullptr;
}

void TextRenderResources::store_raster_cache(uint64_t key, std::shared_ptr<TextRasterization> result) {
    if (!raster_cache) return;
    size_t weight = static_cast<size_t>(result->image.width()) *
                    static_cast<size_t>(result->image.height()) * 4;
    std::unique_lock lock(raster_cache->mutex);
    raster_cache->cache.put(key, result, weight);
}

// ═══════════════════════════════════════════════════════════════════════════
// P1-9 — GlyphAtlas per-instance API
// ═══════════════════════════════════════════════════════════════════════════
//
// `glyph_atlas` is allocated lazily on the first `set_glyph_atlas_capacity`
// call (called by SoftwareRenderer ctor).  Subsequent lookups/store/clear
// no-op on an un-materialized instance.  The 4 free functions
// (`set/get_glyph_atlas` + `get_glyph_atlas_mutex` + `glyph_atlas_clear`)
// are deleted; callers must use these member functions.

void TextRenderResources::set_glyph_atlas_capacity(size_t max_bytes) {
    if (!glyph_atlas) {
        glyph_atlas = std::make_unique<detail::GlyphAtlasCache>(max_bytes);
    }
    // Post-materialization calls are silently ignored — see GlyphAtlasCache
    // ctor comment for the rationale (legacy first-call-wins semantics).
}

void TextRenderResources::ensure_glyph_atlas_materialized() {
    // P1-9 fix-up: thread-safe lazy materialization with the 32 MiB
    // fallback (matches the legacy `resolve_atlas_max_bytes()` default
    // in the deleted `glyph_atlas.cpp`).  Without this, every lookup
    // returns nullopt in production because the renderer ctor never
    // called `set_glyph_atlas_capacity` (BLOCKING-1 in P1-9 review).
    // `std::call_once` is thread-safe under concurrent first-access
    // from multiple render threads; the per-instance `once_flag`
    // ensures each `TextRenderResources` has its own materialization
    // state.  Idempotent: post-materialization calls are no-ops.
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
    // TICKET-TEXT-TIMING-V1 — time the per-glyph atlas lookup so a cache
    // probe that regressed into re-rasterization is visible in telemetry.
    const auto lookup_t0 = profiling::now();
    ensure_glyph_atlas_materialized();
    detail::GlyphAtlasKey key{font_path, glyph_id, font_size};
    std::shared_lock lock(glyph_atlas->mutex);
    auto result = glyph_atlas->cache.get(key);
    if (profiling::g_current_counters) {
        profiling::g_current_counters->glyph_cache_lookup_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(lookup_t0))),
            std::memory_order_relaxed);
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
    const auto store_t0 = profiling::now();
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
    detail::GlyphAtlasKey key{font_path, glyph_id, font_size};
    std::unique_lock lock(glyph_atlas->mutex);
    glyph_atlas->cache.put(key, entry, weight);
    if (profiling::g_current_counters) {
        profiling::g_current_counters->glyph_atlas_upload_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(store_t0))),
            std::memory_order_relaxed);
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
