#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/cache/cache_policy.hpp>

#include <algorithm>
#include <utility>

namespace chronon3d::cache {

namespace {

[[nodiscard]] size_t bytes_for_format(i32 width, i32 height, VideoPixelFormat format) {
    const size_t w = static_cast<size_t>(std::max<i32>(width, 0));
    const size_t h = static_cast<size_t>(std::max<i32>(height, 0));
    switch (format) {
        case VideoPixelFormat::RGBA8:
            return w * h * 4u;
        case VideoPixelFormat::YUV420P:
        case VideoPixelFormat::NV12:
            return w * h * 3u / 2u;
    }
    return 0;
}

[[nodiscard]] u64 hash_combine(u64 seed, u64 value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}


} // namespace

VideoFrame::VideoFrame(i32 width, i32 height, VideoPixelFormat format) {
    resize(width, height, format);
}

size_t VideoFrame::expected_size() const {
    return bytes_for_format(m_width, m_height, m_format);
}

void VideoFrame::resize(i32 width, i32 height, VideoPixelFormat format) {
    m_width = width;
    m_height = height;
    m_format = format;
    m_bytes.assign(bytes_for_format(width, height, format), 0);
}

u64 VideoFrameKey::digest() const {
    u64 seed = std::hash<std::string>{}(composition_id);
    seed = hash_combine(seed, frame_index);
    seed = hash_combine(seed, static_cast<u64>(width));
    seed = hash_combine(seed, static_cast<u64>(height));
    seed = hash_combine(seed, static_cast<u64>(format));
    seed = hash_combine(seed, scene_hash);
    seed = hash_combine(seed, render_hash);
    return seed;
}

size_t VideoFrameKeyHash::operator()(const VideoFrameKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

VideoFrameCache::VideoFrameCache(size_t max_entries, size_t num_shards)
    : m_cache(
          resolve_cache_policy(CacheDomain::VideoFrames,
                               max_entries > 0 ? std::optional<std::size_t>(max_entries) : std::nullopt).capacity,
          num_shards,
          capacity_mode_for(CacheDomain::VideoFrames))
{}

bool VideoFrameCache::contains(const VideoFrameKey& key) const {
    return m_cache.contains(key);
}

std::shared_ptr<VideoFrame> VideoFrameCache::find(const VideoFrameKey& key) {
    auto opt = m_cache.get(key);
    if (!opt) return nullptr;
    return *std::move(opt);
}

void VideoFrameCache::store(VideoFrameKey key, Value value) {
    // Count mode overrides weight to 1; pass 1 for clarity.
    m_cache.put(std::move(key), std::move(value), /*weight=*/1);
}

bool VideoFrameCache::erase(const VideoFrameKey& key) {
    return m_cache.erase(key);
}

void VideoFrameCache::clear() {
    m_cache.clear();
}

size_t VideoFrameCache::size() const {
    return m_cache.stats().current_size;
}

LruCache<VideoFrameKey,
         std::shared_ptr<VideoFrame>,
         VideoFrameKeyHash>::Stats VideoFrameCache::stats() const {
    return m_cache.stats();
}

} // namespace chronon3d::cache
