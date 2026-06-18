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
// CapacityMode::Count.  lookup() returns shared_ptr<const Entry>; a null
// shared_ptr means miss.  When the cache evicts or is cleared an entry can
// still outlive eviction if the caller holds its shared_ptr.
//
// Thread-safety: YES — sharded LRU with per-shard mutex.  Safe to access
// from multiple encoder threads (one shard per thread typically).
// ---------------------------------------------------------------------------

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
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
/// `data.size()` is the authoritative byte length; `data_size` is a
/// synonym kept for backwards-compatibility with the pre-LruCache impl
/// (the two were independent in the original: data.capacity held extra
/// padding not exposed via data_size).  After the LruCache migration
/// `data.size()` always equals `data_size`.
struct ConvertedFrameCacheEntry {
    ConvertedFrameCacheKey key;
    std::vector<uint8_t>   data;
    std::size_t            data_size{0};    ///< valid bytes in data (== data.size())
};

/// Sharded LRU cache of converted encoder frames.
///
/// Backed by LruCache<ConvertedFrameCacheKey,
///                   std::shared_ptr<ConvertedFrameCacheEntry>> in
/// CapacityMode::Count.  lookup() returns a shared_ptr<const Entry>;
/// a null shared_ptr means "miss".  Returned shared_ptrs keep the entry
/// alive even if a subsequent insert would have evicted it.
///
/// Thread-safety: YES — sharded LRU with per-shard mutex.  Multiple
/// encoder threads can call lookup/insert concurrently.
///
/// Capacity resolution (priority: explicit ctor arg > config > hardcoded
/// fallback of 8 entries, which covers ±4 frame seek without recomputing).
class ConvertedFrameCache {
public:
    /// Hardcoded fallback used when neither the caller's ctor argument nor
    /// Config::get().converted_frame_cache_max_entries supply a value.
    /// 8 entries covers ±4 frame seek without recomputing.
    static constexpr std::size_t kDefaultEntryCap = 8;

    /// Hardcoded fallback shard count.
    /// 2 shards strike a balance between per-thread parallelism and
    /// bookkeeping overhead; tests that need strict "evict at N" semantics
    /// can pass num_shards=1 to keep the total cap equal to N.
    static constexpr std::size_t kDefaultNumShards = 2;

    /// Construct with an explicit max-entries cap.  Pass 0 to defer to
    /// Config::get().converted_frame_cache_max_entries (env override
    /// CHRONON3D_CONVERTED_FRAME_CACHE_MAX_ENTRIES) or the hardcoded
    /// fallback.  `num_shards` defaults to 2; tests may pass 1 when
    /// the per-shard split would otherwise hide the expected eviction.
    explicit ConvertedFrameCache(
        std::size_t max_entries = 0,
        std::size_t num_shards  = kDefaultNumShards);

    /// Look up a key.  Returns nullptr-shared_ptr on miss.
    /// On hit returns a shared_ptr<const Entry> that keeps the entry
    /// alive even if the LRU later evicts it.
    [[nodiscard]] std::shared_ptr<const ConvertedFrameCacheEntry>
    lookup(const ConvertedFrameCacheKey& key);

    /// Store a newly converted frame, copying `data[0..data_size]` into
    /// a freshly allocated Entry.  If the key already exists its payload
    /// is replaced in place (the shared_ptr identity may change).  Evicts
    /// the LRU entry if at capacity.
    void insert(
        const ConvertedFrameCacheKey& key,
        const uint8_t* data,
        std::size_t    data_size);

    /// Move-in alternative: hand us the bytes we already allocated in
    /// the caller (avoids a heap copy).  Returns a shared_ptr<const
    /// Entry> referring to the stored entry (may be the same ptr that
    /// was passed in, or a newly-allocated replacement).
    std::shared_ptr<const ConvertedFrameCacheEntry>
    put_entry(
        ConvertedFrameCacheKey key,
        std::vector<uint8_t>    data);

    void clear();

    // ── Diagnostics ────────────────────────────────────────────────────
    [[nodiscard]] std::size_t hits()   const noexcept;
    [[nodiscard]] std::size_t misses() const noexcept;
    [[nodiscard]] std::size_t size()   const noexcept;
    /// Aggregated stats.  Return type deduced from the LruCache's Stats
    /// struct so changes to the LruCache's Stats layout propagate here.
    /// Implementation in .cpp uses plain `auto` (trailing return type would
    /// need `m_cache` to be visible from outside the class scope, which it
    /// is not when the return-type computation runs).
    [[nodiscard]] auto stats() const noexcept;

private:
    /// Resolve the max entries: max_entries > 0 ? max_entries :
    /// Config::get().converted_frame_cache_max_entries > 0 ? that : kDefaultEntryCap.
    static std::size_t resolve_max_entries(std::size_t caller_value);

    chronon3d::cache::LruCache<
        ConvertedFrameCacheKey,
        std::shared_ptr<ConvertedFrameCacheEntry>,
        ConvertedFrameCacheKeyHash>
        m_cache;
};

// Hash for ConvertedFrameCacheKey — used as the shard-routing hash by
// LruCache<ConvertedFrameCacheKey, …, ConvertedFrameCacheKeyHash>.
struct ConvertedFrameCacheKeyHash {
    [[nodiscard]] std::size_t operator()(
        const ConvertedFrameCacheKey& k) const noexcept;
};

} // namespace chronon3d::video
