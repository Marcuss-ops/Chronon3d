#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

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
    return core::hash::HashBuilder{}
        .add(composition_id)
        .add(frame_index)
        .add(width)
        .add(height)
        .add_enum(format)
        .add(scene_hash)
        .add(render_hash)
        .finish();
}

size_t VideoFrameKeyHash::operator()(const VideoFrameKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

VideoFrameCache::VideoFrameCache(size_t max_entries, size_t num_shards, CacheDiagnostics* diag)
    : m_cache(
          [&] {
              auto p = resolve_cache_policy(CacheDomain::VideoFrames,
                  max_entries > 0 ? std::optional<std::size_t>(max_entries) : std::nullopt);
              if (diag) {
                  m_diag_handle = diag->register_cache(
                      CacheDomain::VideoFrames,
                      [this]() -> GenericCacheStats {
                          auto s = m_cache.stats();
                          return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
                      },
                      [this] { m_cache.clear(); },
                      [this] { return m_cache.capacity_mode(); },
                      p.capacity);
              }
              return p;
          }().capacity,
          num_shards,
          capacity_mode_for(CacheDomain::VideoFrames))
{}

void VideoFrameCache::set_diagnostics(CacheDiagnostics& diag) {
    m_diag_alive.store(false, std::memory_order_release);
    m_diag_handle = {};
    m_diag_alive.store(true, std::memory_order_release);
    m_diag_handle = diag.register_cache(
        CacheDomain::VideoFrames,
        [this]() -> GenericCacheStats {
            if (!m_diag_alive.load(std::memory_order_acquire)) return {};
            auto s = m_cache.stats();
            return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return;
            m_cache.clear();
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Weight;
            return m_cache.capacity_mode();
        },
        resolve_cache_policy(CacheDomain::VideoFrames, std::nullopt).capacity);
}

bool VideoFrameCache::contains(const VideoFrameKey& key) const {
    return m_cache.contains(key);
}

std::shared_ptr<VideoFrame> VideoFrameCache::find(const VideoFrameKey& key) {
    auto opt = m_cache.get(key);
    if (!opt) return nullptr;
    return *std::move(opt);
}

void VideoFrameCache::store(VideoFrameKey key, Value value) {
    const auto weight = value ? value->size() : 0;
    m_cache.put(std::move(key), std::move(value), weight);
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
