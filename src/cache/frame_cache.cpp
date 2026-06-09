#include <chronon3d/cache/frame_cache.hpp>
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
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(width));
    seed = frame_cache_detail::hash_combine(seed, frame_cache_detail::hash_value(height));
    seed = frame_cache_detail::hash_combine(seed, scene_hash);
    seed = frame_cache_detail::hash_combine(seed, render_hash);
    return seed;
}

size_t FrameCacheKeyHash::operator()(const FrameCacheKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

bool FrameCache::contains(const FrameCacheKey& key) const {
    return m_entries.contains(key);
}

const FrameCache::Value* FrameCache::find(const FrameCacheKey& key) const {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return nullptr;
    }
    return &it->second;
}

void FrameCache::store(FrameCacheKey key, Value value) {
    m_entries.insert_or_assign(std::move(key), std::move(value));
}

bool FrameCache::erase(const FrameCacheKey& key) {
    return m_entries.erase(key) > 0;
}

void FrameCache::clear() {
    m_entries.clear();
}

} // namespace chronon3d::cache
