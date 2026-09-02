// =============================================================================
// video_frame_cache.hpp — ContentCache: converted video frames
//
// Cache family: ContentCache (see cache/cache_taxonomy.hpp).
// =============================================================================
#pragma once

// =============================================================================
// LruCache-backed cache of converted video frames keyed by
// (composition_id, frame_index, dimensions, canonical FrameFormat,
// scene_hash, render_hash).
//
// Pixel/color semantics are owned by runtime::FrameFormat. The cache no longer
// defines a parallel video pixel taxonomy.
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/runtime/frame_format.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cache {

struct VideoFrameKey {
    std::string composition_id;
    u64 frame_index{0};
    i32 width{0};
    i32 height{0};
    runtime::FrameFormat format{
        runtime::make_frame_format(runtime::PixelFormat::Yuv420P)};
    u64 scene_hash{0};
    u64 render_hash{0};

    [[nodiscard]] bool operator==(const VideoFrameKey&) const = default;
    [[nodiscard]] u64 digest() const;
};

struct VideoFrameKeyHash {
    [[nodiscard]] size_t operator()(const VideoFrameKey& key) const noexcept;
};

/// Raw bytes for one converted video frame. Format semantics are canonical and
/// shared with runtime/media boundaries rather than re-declared by the cache.
class VideoFrame {
public:
    VideoFrame() = default;
    VideoFrame(i32 width, i32 height, runtime::FrameFormat format);

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] const runtime::FrameFormat& format() const { return m_format; }
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return m_bytes; }
    [[nodiscard]] std::vector<uint8_t>& bytes() { return m_bytes; }
    [[nodiscard]] const uint8_t* data() const { return m_bytes.data(); }
    [[nodiscard]] uint8_t* data() { return m_bytes.data(); }
    [[nodiscard]] size_t size() const { return m_bytes.size(); }
    [[nodiscard]] bool empty() const { return m_bytes.empty(); }

    [[nodiscard]] size_t expected_size() const;
    void resize(i32 width, i32 height, runtime::FrameFormat format);

private:
    i32 m_width{0};
    i32 m_height{0};
    runtime::FrameFormat m_format{
        runtime::make_frame_format(runtime::PixelFormat::Yuv420P)};
    std::vector<uint8_t> m_bytes;
};

/// Thread-safe (sharded) LRU-bounded cache of converted video frames.
class VideoFrameCache {
public:
    using Value = std::shared_ptr<VideoFrame>;

    /// When `max_entries == 0` the cap is resolved centrally via
    /// resolve_cache_policy(CacheDomain::VideoFrames).
    explicit VideoFrameCache(size_t max_entries = 0,
                             size_t num_shards  = 2,
                             CacheDiagnostics* diag = nullptr);
    void set_diagnostics(CacheDiagnostics& diag);
    VideoFrameCache(VideoFrameCache&&) noexcept = default;
    VideoFrameCache& operator=(VideoFrameCache&&) noexcept = default;
    ~VideoFrameCache() { m_diag_alive.store(false, std::memory_order_release); }

    [[nodiscard]] bool contains(const VideoFrameKey& key) const;
    [[nodiscard]] std::shared_ptr<VideoFrame> find(const VideoFrameKey& key);
    void store(VideoFrameKey key, Value value);
    [[nodiscard]] bool erase(const VideoFrameKey& key);
    void clear();
    [[nodiscard]] size_t size() const;
    [[nodiscard]] LruCache<VideoFrameKey, Value, VideoFrameKeyHash>::Stats stats() const;

private:
    CacheDiagnostics::Handle m_diag_handle;
    LruCache<VideoFrameKey, Value, VideoFrameKeyHash> m_cache;
    std::atomic<bool> m_diag_alive{true};
};

} // namespace chronon3d::cache
