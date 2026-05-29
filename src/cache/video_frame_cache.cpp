#include <chronon3d/cache/video_frame_cache.hpp>

#include <algorithm>
#include <utility>

namespace chronon3d::cache {

namespace {

constexpr size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

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

bool VideoFrameCache::contains(const VideoFrameKey& key) const {
    return m_entries.contains(key);
}

const VideoFrameCache::Value* VideoFrameCache::find(const VideoFrameKey& key) const {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return nullptr;
    }
    return &it->second;
}

void VideoFrameCache::store(VideoFrameKey key, Value value) {
    m_entries.insert_or_assign(std::move(key), std::move(value));
}

bool VideoFrameCache::erase(const VideoFrameKey& key) {
    return m_entries.erase(key) > 0;
}

void VideoFrameCache::clear() {
    m_entries.clear();
}

} // namespace chronon3d::cache
