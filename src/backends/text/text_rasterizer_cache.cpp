#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#include <memory>
#include <shared_mutex>

namespace chronon3d {

namespace {

using CacheKey = u64;
using TextCache = cache::LruCache<CacheKey, std::shared_ptr<TextRasterization>>;

TextCache& get_text_cache() {
    static TextCache cache(Config::get().text_cache_max_bytes, 8);
    return cache;
}

std::shared_mutex& get_text_cache_mutex() {
    static std::shared_mutex mutex;
    return mutex;
}

} // anonymous namespace

CacheKey hash_text_style(const TextShape& t, float effective_size, int padding, const Mat4* transform) {
    return graph::hash_text_style_full(t, effective_size, padding, transform);
}

bool lookup_text_cache(const CacheKey& key, std::shared_ptr<TextRasterization>& out) {
    std::shared_lock lock(get_text_cache_mutex());
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
    std::unique_lock lock(get_text_cache_mutex());
    get_text_cache().put(key, result, weight);
}

void clear_text_raster_cache() {
    std::unique_lock lock(get_text_cache_mutex());
    get_text_cache().clear();
}

} // namespace chronon3d
