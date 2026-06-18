#pragma once

// =============================================================================
// video_frame_cache.hpp — LruCache-backed cache of encoded video frames
// (RGBA8/YUV420P/NV12) keyed by (composition_id, frame_index, dimensions,
// format, scene_hash, render_hash).
//
// Commit 2 of the cache refactor: previously this was an `unordered_map` with
// NO eviction (unbounded growth).  After the collapse it is backed by LruCache
// in CapacityMode::Weight (byte-weighted via VideoFrame::size()) with capacity
// resolved centrally by resolve_cache_policy(CacheDomain::VideoFrames).
//
// Breaking API change (zero prod callers currently):
//   find() now returns std::shared_ptr<VideoFrame> (nullptr on miss) instead
//   of a pointer into a stable unordered_map.
// =============================================================================

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cache {

enum class VideoPixelFormat {
    RGBA8,
    YUV420P,
    NV12,
};

struct VideoFrameKey {
    std::string composition_id;
    u64 frame_index{0};
    i32 width{0};
    i32 height{0};
    VideoPixelFormat format{VideoPixelFormat::YUV420P};
    u64 scene_hash{0};
    u64 render_hash{0};

    [[nodiscard]] bool operator==(const VideoFrameKey&) const = default;
    [[nodiscard]] u64 digest() const;
};

struct VideoFrameKeyHash {
    [[nodiscard]] size_t operator()(const VideoFrameKey& key) const noexcept;
};

/// Raw encoded bytes for one video frame.
class VideoFrame {
public:
    VideoFrame() = default;
    VideoFrame(i32 width, i32 height, VideoPixelFormat format);

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] VideoPixelFormat format() const { return m_format; }
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return m_bytes; }
    [[nodiscard]] std::vector<uint8_t>& bytes() { return m_bytes; }
    [[nodiscard]] const uint8_t* data() const { return m_bytes.data(); }
    [[nodiscard]] uint8_t* data() { return m_bytes.data(); }
    [[nodiscard]] size_t size() const { return m_bytes.size(); }
    [[nodiscard]] bool empty() const { return m_bytes.empty(); }

    [[nodiscard]] size_t expected_size() const;
    void resize(i32 width, i32 height, VideoPixelFormat format);

private:
    i32 m_width{0};
    i32 m_height{0};
    VideoPixelFormat m_format{VideoPixelFormat::YUV420P};
    std::vector<uint8_t> m_bytes;
};

/// Thread-safe (sharded) LRU-bounded cache of encoded video frames.
class VideoFrameCache {
public:
    using Value = std::shared_ptr<VideoFrame>;

    /// See FrameCache ctor for Config-driven fallback semantics.
    /// When `max_entries == 0` the cap is resolved centrally via
    /// resolve_cache_policy(CacheDomain::VideoFrames).
    explicit VideoFrameCache(size_t max_entries = 0, size_t num_shards = 2);
    VideoFrameCache(VideoFrameCache&&) noexcept = default;
    VideoFrameCache& operator=(VideoFrameCache&&) noexcept = default;

    [[nodiscard]] bool contains(const VideoFrameKey& key) const;
    /// Look up a key. Promotes the entry to MRU on hit, so this method is
    /// intentionally non-const (matches LruCache::get's semantics).
    [[nodiscard]] std::shared_ptr<VideoFrame> find(const VideoFrameKey& key);
    void store(VideoFrameKey key, Value value);
    [[nodiscard]] bool erase(const VideoFrameKey& key);
    void clear();
    [[nodiscard]] size_t size() const;
    [[nodiscard]] LruCache<VideoFrameKey, Value, VideoFrameKeyHash>::Stats stats() const;

private:
    LruCache<VideoFrameKey, Value, VideoFrameKeyHash> m_cache;
};

} // namespace chronon3d::cache
