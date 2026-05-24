#pragma once

// ---------------------------------------------------------------------------
// converted_frame_cache.hpp — LRU cache of already-converted encoder frames.
//
// Key insight: if the same Framebuffer digest is presented again (static frame,
// freeze frame, looped intro/outro) with the same format + geometry, we can
// skip the expensive RGBA→YUV conversion entirely and re-use the packed bytes.
//
// Cache key = {digest, width, height, format, color_matrix, apply_gamma}
// Eviction   = LRU (least-recently-used frame index)
// ---------------------------------------------------------------------------

#include <chronon3d/video/frame_converter.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::video {

/// Identity of a converted frame.  Two requests with equal keys yield
/// bit-identical output and can share a cache entry.
struct ConvertedFrameCacheKey {
    uint64_t framebuffer_digest{0};
    int      width{0};
    int      height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
    int      color_matrix{0};
    bool     apply_gamma{true};

    [[nodiscard]] bool operator==(const ConvertedFrameCacheKey& o) const noexcept {
        return framebuffer_digest == o.framebuffer_digest
            && width  == o.width
            && height == o.height
            && format == o.format
            && color_matrix == o.color_matrix
            && apply_gamma == o.apply_gamma;
    }
};

/// A single resident cache entry.
struct ConvertedFrameCacheEntry {
    ConvertedFrameCacheKey key;
    std::vector<uint8_t>   data;
    size_t                 data_size{0};    ///< valid bytes in data (may be < data.capacity())
    uint64_t               last_used{0};   ///< logical access counter for LRU eviction
};

/// Fixed-capacity LRU cache of converted encoder frames.
///
/// Thread-safety: NOT thread-safe.  Callers must serialise access (the writer
/// thread in video_export_pipe already owns this exclusively).
class ConvertedFrameCache {
public:
    /// Default capacity: 8 entries covers ±4 frame seek without recomputing.
    static constexpr size_t kDefaultCapacity = 8;

    explicit ConvertedFrameCache(size_t capacity = kDefaultCapacity);

    /// Look up a key.  Returns a pointer to the entry (valid until the next
    /// insert() or clear()) or nullptr on miss.
    [[nodiscard]] const ConvertedFrameCacheEntry* lookup(
        const ConvertedFrameCacheKey& key);

    /// Store a newly converted frame.  Evicts the LRU entry if at capacity.
    void insert(
        const ConvertedFrameCacheKey& key,
        const uint8_t* data,
        size_t         data_size);

    void clear();

    // ── Diagnostics ────────────────────────────────────────────────────
    [[nodiscard]] size_t hits()   const noexcept { return hits_;   }
    [[nodiscard]] size_t misses() const noexcept { return misses_; }
    [[nodiscard]] size_t size()   const noexcept { return entries_.size(); }

private:
    std::vector<ConvertedFrameCacheEntry> entries_;
    size_t   capacity_;
    uint64_t access_clock_{0};  ///< monotonically increasing access counter
    size_t   hits_{0};
    size_t   misses_{0};

    size_t find_lru_slot() const;
};

} // namespace chronon3d::video
