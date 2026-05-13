#include <chronon3d/cache/frame_cache.hpp>

#include <string_view>
#include <utility>

namespace chronon3d::cache {

namespace {

[[nodiscard]] u64 hash_string(std::string_view value) {
    return XXH3_64bits(value.data(), value.size());
}

template <typename T>
[[nodiscard]] u64 hash_value(const T& value) {
    return XXH3_64bits(&value, sizeof(T));
}

[[nodiscard]] u64 hash_combine(u64 seed, u64 value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

} // namespace

u64 FrameCacheKey::digest() const {
    u64 seed = hash_string(composition_id);
    seed = hash_combine(seed, hash_value(frame));
    seed = hash_combine(seed, hash_value(width));
    seed = hash_combine(seed, hash_value(height));
    seed = hash_combine(seed, scene_hash);
    seed = hash_combine(seed, render_hash);
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
