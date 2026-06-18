#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/cache/cache_policy.hpp>
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
          resolve_cache_policy(CacheDomain::RenderedFrames,
                               max_entries > 0 ? std::optional<std::size_t>(max_entries) : std::nullopt).capacity,
          num_shards,
          capacity_mode_for(CacheDomain::RenderedFrames))
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
    const auto weight = value ? value->size_bytes() : 0;
    m_cache.put(std::move(key), std::move(value), weight);
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
