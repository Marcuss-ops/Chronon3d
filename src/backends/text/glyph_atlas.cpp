#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <blend2d.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <shared_mutex>
#include <string>

namespace chronon3d {

namespace {

// ── Glyph cache key ────────────────────────────────────────────────────
struct GlyphAtlasKey {
    std::string font_path;
    u32         glyph_id{0};
    u32         font_size{0};

    bool operator==(const GlyphAtlasKey& o) const noexcept {
        return font_path == o.font_path &&
               glyph_id == o.glyph_id &&
               font_size == o.font_size;
    }
};

struct GlyphAtlasKeyHash {
    [[nodiscard]] size_t operator()(const GlyphAtlasKey& k) const noexcept {
        size_t h = std::hash<std::string>{}(k.font_path);
        h ^= std::hash<u32>{}(k.glyph_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.font_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// ── LRU cache with 8 shards, 32 MB default ────────────────────────────
size_t resolve_atlas_max_mb() {
    auto max_bytes = Config::get().glyph_atlas_max_bytes;
    return max_bytes > 0 ? max_bytes : 32ULL * 1024ULL * 1024ULL;
}

using GlyphAtlasCache = cache::LruCache<GlyphAtlasKey, std::shared_ptr<BLImage>,
                                         GlyphAtlasKeyHash>;

GlyphAtlasCache& get_glyph_atlas() {
    static GlyphAtlasCache atlas(resolve_atlas_max_mb(), 8);
    return atlas;
}

std::shared_mutex& get_glyph_atlas_mutex() {
    static std::shared_mutex mtx;
    return mtx;
}

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────────

std::optional<GlyphAtlasEntry> glyph_atlas_lookup(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size
) {
    GlyphAtlasKey key{font_path, glyph_id, font_size};
    std::shared_lock lock(get_glyph_atlas_mutex());
    auto cached = get_glyph_atlas().get(key);
    if (cached) {
        GlyphAtlasEntry entry;
        entry.image = *cached;
        return entry;
    }
    return std::nullopt;
}

void glyph_atlas_store(
    const std::string& font_path,
    u32 glyph_id,
    u32 font_size,
    const GlyphAtlasEntry& entry
) {
    GlyphAtlasKey key{font_path, glyph_id, font_size};
    size_t weight = static_cast<size_t>(entry.image->width()) *
                    static_cast<size_t>(entry.image->height()) * 4;
    auto img_copy = std::make_shared<BLImage>(*entry.image);
    std::unique_lock lock(get_glyph_atlas_mutex());
    get_glyph_atlas().put(key, img_copy, weight);
}

void glyph_atlas_clear() {
    std::unique_lock lock(get_glyph_atlas_mutex());
    get_glyph_atlas().clear();
}

GlyphAtlasStats glyph_atlas_stats() {
    GlyphAtlasStats s;
    std::shared_lock lock(get_glyph_atlas_mutex());
    auto st = get_glyph_atlas().stats();
    s.entry_count  = st.current_size;
    s.total_weight = st.current_weight;
    s.hits         = st.hits;
    s.misses       = st.misses;
    return s;
}

void glyph_atlas_store_from_text(
    const std::string& font_path,
    const BLImage& rendered_text,
    const BLGlyphBuffer& gb,
    const BLFont& font,
    float text_origin_x,
    float text_origin_y,
    float font_size
) {
    BLImageData img_data;
    if (rendered_text.getData(&img_data) != BL_SUCCESS) return;
    if (!img_data.pixelData || img_data.size.w <= 0 || img_data.size.h <= 0) return;

    const int img_w = img_data.size.w;
    const int img_h = img_data.size.h;
    const int stride = static_cast<int>(img_data.stride / sizeof(uint32_t));

    // Iterate glyphs in the buffer and extract each glyph's bitmap
    const BLGlyphRun& run = gb.glyphRun();
    if (!run.glyphData || !run.placementData) return;

    const auto* glyph_ids = static_cast<const uint32_t*>(run.glyphData);
    const auto* placements = static_cast<const BLGlyphPlacement*>(run.placementData);

    for (size_t i = 0; i < run.size; ++i) {
        const u32 glyph_id = glyph_ids[i];
        if (glyph_id == 0) continue; // skip .notdef

        // Get glyph placement from the run (already computed by Blend2D)
        const BLGlyphPlacement& placement = placements[i];

        // Get glyph bounds via batch API (single glyph)
        BLBoxI bbox_i;
        uint32_t gid = glyph_id;
        if (font.getGlyphBounds(&gid, 0, &bbox_i, 1) != BL_SUCCESS) continue;
        const BLBox bbox{static_cast<double>(bbox_i.x0), static_cast<double>(bbox_i.y0),
                         static_cast<double>(bbox_i.x1), static_cast<double>(bbox_i.y1)};

        // Glyph position in the rendered image
        const int gx = static_cast<int>(std::floor(text_origin_x + placement.placement.x));
        const int gy = static_cast<int>(std::floor(text_origin_y + placement.placement.y));

        // Bounding box of the glyph in the image
        const int gx0 = gx + static_cast<int>(std::floor(bbox.x0));
        const int gy0 = gy + static_cast<int>(std::floor(bbox.y0));
        const int gx1 = gx + static_cast<int>(std::ceil(bbox.x1));
        const int gy1 = gy + static_cast<int>(std::ceil(bbox.y1));

        const int gw = gx1 - gx0;
        const int gh = gy1 - gy0;
        if (gw <= 0 || gh <= 0) continue;
        if (gx0 < 0 || gy0 < 0 || gx1 > img_w || gy1 > img_h) continue;

        // Extract glyph bitmap
        auto glyph_img = std::make_shared<BLImage>(gw, gh, BL_FORMAT_PRGB32);
        {
            BLImageData glyph_data;
            if (glyph_img->getData(&glyph_data) != BL_SUCCESS) continue;
            auto* dst = static_cast<uint32_t*>(glyph_data.pixelData);
            auto* src = static_cast<const uint32_t*>(img_data.pixelData);
            const int gs = static_cast<int>(glyph_data.stride / sizeof(uint32_t));

            for (int y = 0; y < gh; ++y) {
                for (int x = 0; x < gw; ++x) {
                    dst[y * gs + x] = src[(gy0 + y) * stride + (gx0 + x)];
                }
            }
        }

        GlyphAtlasEntry entry;
        entry.image     = glyph_img;
        entry.x_offset  = gx0;
        entry.y_offset  = gy0;
        entry.advance_x = static_cast<float>(placement.advance.x);

        glyph_atlas_store(font_path, glyph_id, static_cast<u32>(std::ceil(font_size)), entry);
    }
}

void glyph_atlas_store_from_placed_run(
    const std::string& font_path,
    const BLImage& rendered_text,
    const PlacedGlyphRun& placed,
    const BLFont& font,
    float origin_x,
    float origin_y,
    float font_size,
    u32 fill_color_rgba
) {
    BLImageData img_data;
    if (rendered_text.getData(&img_data) != BL_SUCCESS) return;
    if (!img_data.pixelData || img_data.size.w <= 0 || img_data.size.h <= 0) return;

    const int img_w = img_data.size.w;
    const int img_h = img_data.size.h;
    const int stride = static_cast<int>(img_data.stride / sizeof(uint32_t));
    const auto* src = static_cast<const uint32_t*>(img_data.pixelData);
    const u32 fs = static_cast<u32>(std::ceil(font_size));

    for (const auto& pg : placed.glyphs) {
        if (pg.glyph_id == 0) continue;

        // Skip if already cached with matching color
        auto existing = glyph_atlas_lookup(font_path, pg.glyph_id, fs);
        if (existing && existing->fill_color_rgba == fill_color_rgba) continue;

        BLBoxI bbox_i;
        uint32_t gid = pg.glyph_id;
        if (font.getGlyphBounds(&gid, 0, &bbox_i, 1) != BL_SUCCESS) continue;
        const BLBox bbox{static_cast<double>(bbox_i.x0), static_cast<double>(bbox_i.y0),
                         static_cast<double>(bbox_i.x1), static_cast<double>(bbox_i.y1)};

        const int gx = static_cast<int>(std::floor(origin_x + pg.x + bbox.x0));
        const int gy = static_cast<int>(std::floor(origin_y + pg.y + bbox.y0));
        const int gw = static_cast<int>(std::ceil(bbox.x1 - bbox.x0));
        const int gh = static_cast<int>(std::ceil(bbox.y1 - bbox.y0));
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
        entry.image     = glyph_img;
        entry.x_offset  = static_cast<int>(bbox.x0);
        entry.y_offset  = static_cast<int>(bbox.y0);
        entry.advance_x = pg.advance_x;
        entry.fill_color_rgba = fill_color_rgba;
        glyph_atlas_store(font_path, pg.glyph_id, fs, entry);

        if (profiling::g_current_counters)
            profiling::g_current_counters->glyph_atlas_stored.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace chronon3d
