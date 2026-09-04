#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <algorithm>
#include <utility>

namespace chronon3d::cache {

VideoFrame::VideoFrame(i32 width, i32 height, runtime::FrameFormat format) {
    resize(width, height, format);
}

size_t VideoFrame::expected_size() const {
    const auto width = static_cast<std::uint32_t>(std::max<i32>(m_width, 0));
    const auto height = static_cast<std::uint32_t>(std::max<i32>(m_height, 0));
    return runtime::tight_surface_bytes(m_format, width, height);
}

void VideoFrame::resize(i32 width, i32 height, runtime::FrameFormat format) {
    m_width = width;
    m_height = height;
    m_format = format;
    m_bytes.assign(expected_size(), 0);
}

u64 VideoFrameKey::digest() const {
    return core::hash::HashBuilder{}
        .add(composition_id)
        .add(frame_index)
        .add(width)
        .add(height)
        .add_enum(format.pixel)
        .add_enum(format.primaries)
        .add_enum(format.transfer)
        .add_enum(format.matrix)
        .add_enum(format.range)
        .add_enum(format.chroma)
        .add_enum(format.alpha)
        .add(format.pixel_aspect.numerator)
        .add(format.pixel_aspect.denominator)
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
                          return make_generic_cache_stats(m_cache.stats());
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
            return make_generic_cache_stats(m_cache.stats());
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