#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <string_view>
#include <utility>

namespace chronon3d::cache {

namespace frame_cache_detail {

using chronon3d::graph::hash_string;
using chronon3d::graph::hash_combine;

template <typename T>
[[nodiscard]] u64 hash_value(const T& value) {
    return chronon3d::graph::hash_value(value);
}

} // namespace frame_cache_detail

namespace {

// Hardcoded fallback when both the caller's argument and Config are zero.
// 256 entries × ~8MB/framebuffer (1080p RGBA) ≈ 2GB ceiling, plenty of headroom
// for typical cinematic exports (a few hundred distinct frames).
constexpr size_t kFrameCacheDefaultEntryCap = 256;

size_t resolve_frame_cache_default_capacity() {
    auto v = Config::get().frame_cache_max_entries;
    return v > 0 ? v : kFrameCacheDefaultEntryCap;
}

} // namespace

u64 FrameCacheKey::digest() const {
    u64 seed = frame_cache_detail::hash_string(composition_id);
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(frame));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(temporal_key.frame));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(temporal_key.subframe_tick));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(temporal_key.version));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(width));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(height));
    seed = frame_cache_detail::hash_combine(seed, scene_hash);
    seed = frame_cache_detail::hash_combine(seed, render_hash);
    return seed;
}

size_t FrameCacheKeyHash::operator()(const FrameCacheKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

FrameCache::FrameCache(size_t max_entries, size_t num_shards)
    : m_cache(
          max_entries > 0 ? max_entries : resolve_frame_cache_default_capacity(),
          num_shards,
          CapacityMode::Count)
{}

bool FrameCache::contains(const FrameCacheKey& key) const {
    return m_cache.contains(key);
}

std::shared_ptr<Framebuffer> FrameCache::find(const FrameCacheKey& key) {
    auto opt = m_cache.get(key);
    if (!opt) return nullptr;
    return *std::move(opt);   // std::shared_ptr move increments the refcount
}

void FrameCache::store(FrameCacheKey key, Value value) {
    // Count mode overrides put's weight to 1; we pass 1 explicitly for clarity.
    // (The cached framebuffer's byte size is irrelevant for count-based caps.)
    m_cache.put(std::move(key), std::move(value), /*weight=*/1);
}

bool FrameCache::erase(const FrameCacheKey& key) {
    return m_cache.erase(key);
}

void FrameCache::clear() {
    m_cache.clear();
}

size_t FrameCache::size() const {
    return m_cache.stats().current_size;
}

LruCache<FrameCacheKey,
         std::shared_ptr<Framebuffer>,
         FrameCacheKeyHash>::Stats FrameCache::stats() const {
    return m_cache.stats();
}

} // namespace chronon3d::cache
