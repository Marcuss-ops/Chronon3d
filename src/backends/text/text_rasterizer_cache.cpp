#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#include <cstdlib>
#include <memory>

namespace chronon3d {

namespace {

using CacheKey = u64;
using TextCache = cache::LruCache<CacheKey, std::shared_ptr<TextRasterization>>;

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_string;

size_t resolve_cache_max_mb(const char* env_name, size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        size_t mb = static_cast<size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
}

TextCache& get_text_cache() {
    static TextCache cache(resolve_cache_max_mb("CHRONON_TEXT_CACHE_MAX_MB", 128), 8);
    return cache;
}

} // anonymous namespace

CacheKey hash_text_style(const TextShape& t, float effective_size, int padding, const Mat4* transform) {
    CacheKey seed = 0;
    seed = hash_combine(seed, hash_string(t.text));
    seed = hash_combine(seed, hash_string(t.style.font_path));
    seed = hash_combine(seed, hash_string(t.style.font_family));
    seed = hash_combine(seed, hash_value(t.style.font_weight));
    seed = hash_combine(seed, hash_string(t.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_value(t.style.color.r));
    seed = hash_combine(seed, hash_value(t.style.color.g));
    seed = hash_combine(seed, hash_value(t.style.color.b));
    seed = hash_combine(seed, hash_value(t.style.color.a));
    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.align)));
    seed = hash_combine(seed, hash_value(t.style.line_height));
    seed = hash_combine(seed, hash_value(t.style.tracking));
    seed = hash_combine(seed, hash_value(t.style.max_lines));
    seed = hash_combine(seed, hash_value(t.style.auto_scale));
    seed = hash_combine(seed, hash_value(t.style.min_size));
    seed = hash_combine(seed, hash_value(t.style.max_size));
    seed = hash_combine(seed, hash_value(t.style.auto_fit));
    seed = hash_combine(seed, hash_value(t.style.ellipsis));
    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.overflow)));
    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.wrap)));
    seed = hash_combine(seed, hash_value(t.box.size.x));
    seed = hash_combine(seed, hash_value(t.box.size.y));
    seed = hash_combine(seed, hash_value(t.box.enabled));
    seed = hash_combine(seed, hash_value(padding));

    seed = hash_combine(seed, hash_value(t.style.paint.fill.r));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.g));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.b));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.a));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_enabled));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.r));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.g));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.b));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.a));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_width));

    seed = hash_combine(seed, hash_value(t.style.box_style.enabled));
    seed = hash_combine(seed, hash_value(t.style.box_style.padding.x));
    seed = hash_combine(seed, hash_value(t.style.box_style.padding.y));
    seed = hash_combine(seed, hash_value(t.style.box_style.radius));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.r));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.g));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.b));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.a));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_enabled));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.r));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.g));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.b));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.a));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_width));

    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.vertical_align)));

    seed = hash_combine(seed, hash_value(t.style.shadows.size()));
    for (const auto& shadow : t.style.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_value(shadow.offset.x));
        seed = hash_combine(seed, hash_value(shadow.offset.y));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_value(shadow.color.r));
        seed = hash_combine(seed, hash_value(shadow.color.g));
        seed = hash_combine(seed, hash_value(shadow.color.b));
        seed = hash_combine(seed, hash_value(shadow.color.a));
    }

    if (transform) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                seed = hash_combine(seed, hash_value((*transform)[i][j]));
            }
        }
    }

    return seed;
}

bool lookup_text_cache(const CacheKey& key, std::shared_ptr<TextRasterization>& out) {
    auto cached = get_text_cache().get(key);
    if (cached) {
        out = *cached;
        return true;
    }
    return false;
}

void store_text_cache(const CacheKey& key, const std::shared_ptr<TextRasterization>& result) {
    size_t weight = static_cast<size_t>(result->image.width()) *
                    static_cast<size_t>(result->image.height()) * 4;
    get_text_cache().put(key, result, weight);
}

void clear_text_raster_cache() {
    get_text_cache().clear();
}

} // namespace chronon3d
