#pragma once

// ---------------------------------------------------------------------------
// converted_frame_cache.hpp — LRU cache of already-converted encoder frames.
//
// Key insight: if the same Framebuffer digest is presented again (static frame,
// freeze frame, looped intro/outro) with the same format + geometry, we can
// skip the expensive RGBA→YUV conversion entirely and re-use the packed bytes.
//
// Cache key = {digest, width, height, format, color_matrix, apply_gamma}
// Eviction   = LRU inside a sharded LruCache (Count mode, 2 shards by default).
//
// Backed by chronon3d::cache::LruCache<…, std::shared_ptr<Entry>> with
// CapacityMode::Weight (byte-weighted via entry->data.size()).  lookup()
// returns a ConvertedFrame view; an empty span means miss.  When the
// cache evicts or is cleared an entry can still outlive eviction if the
// caller holds its shared_ptr.
//
// Thread-safety: YES — sharded LRU with per-shard mutex.  Safe to access
// from multiple encoder threads (one shard per thread typically).
// ---------------------------------------------------------------------------

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace chronon3d::video {

/// Forward declaration so the LruCache<…> template member below can
/// be instantiated with this hash type.  Definition follows the cache
/// class so the operator() body can use the full ConvertedFrameCacheKey
/// type without a circular include dependency.
struct ConvertedFrameCacheKeyHash;

/// Identity of a converted frame.  Two requests with equal keys yield
/// bit-identical output and can share a cache entry.
struct ConvertedFrameCacheKey {
    uint64_t framebuffer_digest{0};
    int      width{0};
    int      height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
    YuvMatrix matrix{YuvMatrix::BT709};
    ColorRange range{ColorRange::Limited};
    bool     apply_gamma{true};

    [[nodiscard]] bool operator==(const ConvertedFrameCacheKey& o) const noexcept {
        return framebuffer_digest == o.framebuffer_digest
            && width  == o.width
            && height == o.height
            && format == o.format
            && matrix == o.matrix
            && range  == o.range
            && apply_gamma == o.apply_gamma;
    }
};

/// A single resident cache entry.
///
/// `data.size()` is the authoritative byte length.
struct ConvertedFrameCacheEntry {
    ConvertedFrameCacheKey key;
    std::vector<uint8_t>   data;
};

/// Non-owning view into a cached converted frame.
///
/// Holds a shared_ptr to the cache entry (keeps it alive even if the
/// LRU evicts it) and a span over the valid bytes.  On a cache miss
/// the shared_ptr may be null and the span points to caller-owned
/// memory (e.g. the destination buffer passed to convert_into).
struct ConvertedFrame {
    std::shared_ptr<const ConvertedFrameCacheEntry> cache_entry;
    std::span<const uint8_t>                        data;
    bool                                              from_cache{false};
    FrameConversionBackend                            backend{FrameConversionBackend::Unavailable};
    uint64_t                                          conversion_ns{0};

    explicit operator bool() const noexcept { return !data.empty(); }
};

/// Sharded LRU cache of converted encoder frames.
///
/// Backed by LruCache<ConvertedFrameCacheKey,
///                   std::shared_ptr<ConvertedFrameCacheEntry>> in
/// CapacityMode::Weight (byte-weighted).  lookup() returns a
/// ConvertedFrame view; an empty span means "miss".  Returned views
/// keep the entry alive even if a subsequent insert would have
/// evicted it.
///
/// Thread-safety: YES — sharded LRU with per-shard mutex.  Multiple
/// encoder threads can call lookup/insert concurrently.
///
/// Capacity resolution: explicit ctor arg > Config/env > hardcoded
/// default (centralized in resolve_cache_policy for ConvertedFrames).
class ConvertedFrameCache {
public:

    /// Construct with an explicit byte-capacity cap.  Pass 0 to defer to
    /// resolve_cache_policy(CacheDomain::ConvertedFrames).
    /// `num_shards` defaults to 2; tests may pass 1 when the per-shard
    /// split would otherwise hide the expected eviction.
    explicit ConvertedFrameCache(
        std::size_t capacity_bytes = 0,
        std::size_t num_shards     = 2);

    /// Look up a key.  Returns an empty ConvertedFrame on miss.
    /// On hit returns a ConvertedFrame view that keeps the entry
    /// alive even if the LRU later evicts it.
    [[nodiscard]] ConvertedFrame lookup(const ConvertedFrameCacheKey& key);

    /// Store a newly converted frame, moving `data` into a freshly
    /// allocated Entry.  If the key already exists its payload is
    /// replaced in place.  Evicts the LRU entry if at capacity.
    /// Returns a ConvertedFrame view into the stored entry.
    ConvertedFrame insert(
        ConvertedFrameCacheKey key,
        std::vector<uint8_t>   data);

    void clear();

private:
    /// Resolve the byte capacity: capacity_bytes > 0 ? capacity_bytes :
    /// instance config cache().converted_frame_cache_max_bytes() > 0 ? that : policy default.
    /// Delegates to the centralized resolve_cache_policy(CacheDomain::ConvertedFrames).
    static std::size_t resolve_capacity_bytes(std::size_t caller_value);

    chronon3d::cache::CacheDiagnostics::Handle m_diag_handle;

    chronon3d::cache::LruCache<
        ConvertedFrameCacheKey,
        std::shared_ptr<ConvertedFrameCacheEntry>,
        ConvertedFrameCacheKeyHash>
        m_cache;

public:
    // ── Diagnostics ────────────────────────────────────────────────────
    [[nodiscard]] std::size_t hits()   const noexcept;
    [[nodiscard]] std::size_t misses() const noexcept;
    [[nodiscard]] std::size_t size()   const noexcept;
    [[nodiscard]] auto stats() const noexcept -> decltype(m_cache.stats());
};

// Hash for ConvertedFrameCacheKey — used as the shard-routing hash by
// LruCache<ConvertedFrameCacheKey, …, ConvertedFrameCacheKeyHash>.
struct ConvertedFrameCacheKeyHash {
    [[nodiscard]] std::size_t operator()(
        const ConvertedFrameCacheKey& k) const noexcept;
};

} // namespace chronon3d::video
