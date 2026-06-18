#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <string_view>
#include <utility>

namespace chronon3d::cache {

u64 FrameCacheKey::digest() const {
    return core::hash::HashBuilder{}
        .add(composition_id)
        .add(frame)
        .add(temporal_key.frame)
        .add(temporal_key.subframe_tick)
        .add(temporal_key.version)
        .add(width)
        .add(height)
        .add(scene_hash)
        .add(render_hash)
        .finish();
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
