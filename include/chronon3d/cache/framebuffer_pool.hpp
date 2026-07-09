#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace chronon3d {
    class FramebufferArena;
}

namespace chronon3d::cache {

// ---------------------------------------------------------------------------
// FramebufferPoolKey
// ---------------------------------------------------------------------------
struct FramebufferPoolKey {
    int width{0};
    int height{0};

    bool operator==(const FramebufferPoolKey&) const = default;
};

struct FramebufferPoolKeyHash {
    size_t operator()(const FramebufferPoolKey& k) const noexcept {
        size_t seed = 1469598103934665603ULL;
        seed ^= static_cast<size_t>(k.width) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(k.height) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// ---------------------------------------------------------------------------
// FramebufferPoolPreallocOptions
// ---------------------------------------------------------------------------
struct FramebufferPoolPreallocOptions {
    int width{0};
    int height{0};
    size_t count{0};
    bool clear{true};
    bool touch_memory{true};
};

// ---------------------------------------------------------------------------
// FramebufferPoolConfig — retention budget and eviction policy
// ---------------------------------------------------------------------------
struct FramebufferPoolConfig {
    // 0 = unlimited (debug/local). Default 384 MB keeps peak memory ~1.0–1.2 GB.
    // Override via --fb-pool-budget-mb CLI flag or CHRONON3D_FB_POOL_BUDGET_MB env.
    size_t max_retained_bytes = 384ULL * 1024ULL * 1024ULL;

    // Avoid one size class keeping too many buffers.
    size_t max_buffers_per_size_class = 4;

    // When true, eviction prefers the least-recently-used entry.
    bool enable_lru_eviction = true;
};

// ---------------------------------------------------------------------------
// PoolEntry — framebuffer with LRU tracking
// ---------------------------------------------------------------------------
struct PoolEntry {
    std::unique_ptr<Framebuffer> fb;
    uint64_t last_used_tick{0};  // monotonic tick from FramebufferPool::m_tick
};

// ---------------------------------------------------------------------------
// FramebufferAcquireHint — guides the pool on lifecycle intent
// ---------------------------------------------------------------------------
enum class FramebufferAcquireHint {
    Default,          // Standard acquire: may clear, may reuse
    ReuseNoClear,     // Caller will overwrite every pixel — skip clear
    Temporary,        // Short-lived intermediate — return to pool immediately after use
};

// ---------------------------------------------------------------------------
// FramebufferPoolStats
// ---------------------------------------------------------------------------
struct FramebufferPoolStats {
    size_t current_bytes{0};
    size_t available_count{0};
    size_t max_bytes{0};
    size_t total_allocations{0};        // lifetime OS allocations
    size_t total_reuses{0};             // lifetime pool reuses
    size_t total_returns{0};            // lifetime returns to pool
    size_t total_clears{0};             // lifetime clear operations
    size_t arena_bytes{0};              // bytes allocated via arena
    double hit_rate{0.0};               // reuse / (alloc + reuse)
    size_t budget_bytes{0};             // configured max_retained_bytes (0 = unlimited)
    size_t retained_bytes{0};           // current bytes retained in pool free list
    size_t evicted_count{0};            // FBs evicted due to budget pressure
    size_t evicted_bytes{0};            // total bytes evicted
    size_t pressure_count{0};           // number of eviction events
    size_t size_class_count{0};         // distinct size classes in the pool
};

// ---------------------------------------------------------------------------
// FramebufferPool
// ---------------------------------------------------------------------------
class FramebufferPool : public std::enable_shared_from_this<FramebufferPool> {
public:
    // Default budget: 384 MB — single source of truth, matches
    // FramebufferPoolConfig::max_retained_bytes.  Override via
    // CHRONON3D_FB_POOL_BUDGET_MB env var or --fb-pool-budget-mb CLI flag.
    static constexpr size_t kDefaultBudgetBytes = 384ULL * 1024ULL * 1024ULL;

    explicit FramebufferPool(size_t max_bytes = kDefaultBudgetBytes);

    /// Construct with explicit config (from CLI/Config system).
    explicit FramebufferPool(const FramebufferPoolConfig& config);
    ~FramebufferPool();

    /// Set a static arena to be used for new allocations.
    void set_arena(std::shared_ptr<chronon3d::FramebufferArena> arena);

    /// Dynamically update the retention budget (0 = unlimited).
    void set_budget_bytes(size_t max_retained_bytes);

    /// Get the current config (thread-safe copy under lock).
    [[nodiscard]] FramebufferPoolConfig config() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config;
    }

    /// Acquire a framebuffer of the requested size.
    /// Clearing is optional and only happens when requested by the caller.
    /// Use acquire_hinted() for finer lifecycle control.
    std::shared_ptr<Framebuffer> acquire(int width, int height, bool clear = true);

    /// Acquire with a lifecycle hint for better pool predictability.
    std::shared_ptr<Framebuffer> acquire_hinted(
        int width, int height,
        FramebufferAcquireHint hint = FramebufferAcquireHint::Default);

    /// Acquire a framebuffer that automatically releases itself back to the pool upon destruction.
    std::shared_ptr<Framebuffer> acquire_pooled(int width, int height, std::shared_ptr<FramebufferPool> pool, bool clear = true);

    /// Acquire a framebuffer as an OwnedFB (unique_ptr with pool deleter).
    /// Zero atomic overhead vs shared_ptr — use in the hot execution path.
    OwnedFB acquire_owned(int width, int height, bool clear = true);

    /// Acquire a framebuffer with no explicit content zeroing.
    ///
    /// Returns a `std::unique_ptr<Framebuffer>` immediately, drawing
    /// from the pool's free list (exact-bucket hit or best-fit reuse)
    /// or constructing a fresh one (against `kDefaultBudgetBytes` /
    /// configured budget).  Unlike `acquire` / `acquire_hinted` /
    /// `acquire_pooled` / `acquire_owned`, this method does NOT call
    /// `fb->clear(Color::transparent())` on a pool reuse — the O(w*h)
    /// zero-fill cost is skipped, which matters at hot-miss rates
    /// during a 1920x1080 RGBA8 frame stream.
    ///
    /// SECURITY CONTRACT (no-info-leak between cache slots):
    ///   When the returned FB came from the pool's free list (the
    ///   common hot-miss case) its pixel content is whatever the
    ///   *previous* user of that buffer wrote.  Callers MUST do ONE
    ///   of the following before reading pixels:
    ///     * call `fb->clear(Color::transparent())` (or any color), OR
    ///     * overwrite every pixel via `set_pixel` / `pixels_row`,
    ///       ensuring no stale byte survives into the consumer pipeline.
    ///
    /// Freshly-constructed FBs (no pool hit) ARE pure-zero by virtue
    /// of `Framebuffer`'s allocator default — that case is not at risk.
    ///
    /// Use this variant only on hot paths where you WILL overwrite
    /// every pixel and the `fb->clear()` cost is provably wasted.  For
    /// safety, default to `acquire(w, h, /*clear=*/true)`.
    std::unique_ptr<Framebuffer> acquire_noclear(int width, int height);

    // ── TICKET-011-final — adapter methods re-introduced for callers
    // that retain the post-PoolFbDeleter-migration API expectations.

    /// Acquire an OwnedFB by copying the pixel contents of `other`.
    OwnedFB acquire_from(const Framebuffer& other);

    /// Adopt a shared_ptr Framebuffer into single-ownership OwnedFB without copying pixels.
    OwnedFB adopt_owned(std::shared_ptr<Framebuffer>&& src);

    /// Convert OwnedFB to CachedFB (shared_ptr) for cache storage.
    CachedFB cache_adopt(OwnedFB owned);

    /// Acquire a framebuffer surfaced as a shared_ptr (cross-API interop).
    std::shared_ptr<Framebuffer> acquire_shared(int width, int height, bool clear = true);

    /// Release a shared_ptr FB back to the pool.
    /// (release_shared removed: zero callers; canonical path is release(Framebuffer*))

    void release(Framebuffer* fb);

    /// Create a shared_ptr-managed pool.  Use this instead of raw construction
    /// to enable weak_from_this() in OwnedFB deleter.
    [[nodiscard]] static std::shared_ptr<FramebufferPool> create_shared(
        size_t max_bytes = kDefaultBudgetBytes) {
        return std::shared_ptr<FramebufferPool>(new FramebufferPool(max_bytes));
    }
    [[nodiscard]] static std::shared_ptr<FramebufferPool> create_shared(
        const FramebufferPoolConfig& config) {
        return std::shared_ptr<FramebufferPool>(new FramebufferPool(config));
    }

    /// Pre-warm the pool with framebuffers matching a predicted usage pattern.
    /// Useful before starting a render to ensure the first frames don't stall on allocation.
    /// Returns total framebuffers pre-allocated.
    size_t warm_up(const std::vector<FramebufferPoolPreallocOptions>& predictions);

    /// Release all pooled framebuffers.
    void clear();

    /// Preallocate framebuffers of a given size into the pool.
    /// Returns the number of framebuffers actually created (may be less than
    /// requested if the pool size limit is reached).
    size_t preallocate(const FramebufferPoolPreallocOptions& options);

    /// Ensure the pool has at least `count` framebuffers of the given bucket size.
    /// Only creates new framebuffers if the existing free count for that bucket
    /// is below the target.  This is safe to call every frame — it will not
    /// grow the pool unboundedly.
    size_t ensure_preallocated(const FramebufferPoolPreallocOptions& options);

    /// Return the bucket-rounded dimensions for a given width/height.
    /// This is the same rounding used internally for pool keys.
    static std::pair<int, int> round_to_bucket(int width, int height);

    [[nodiscard]] size_t current_bytes() const;
    [[nodiscard]] size_t available_count() const;
    [[nodiscard]] size_t max_bytes() const;
    /// Get detailed pool statistics (thread-safe).
    [[nodiscard]] FramebufferPoolStats stats() const;

    /// Reset lifetime counters (allocations, reuses, returns, clears).
    void reset_counters();

private:
    /// Acquire a framebuffer from the pool or allocate a new one.
    /// Sets *out_fresh_alloc to true when the returned FB was freshly
    /// constructed (zero-initialized by the Framebuffer constructor)
    /// and therefore doesn't need explicit clearing.
    /// Public-facing counterparts:
    ///   * `acquire_noclear(w, h)`        — raw, no clear (hot path)
    ///   * `acquire_hinted(w, h, ...)`    — conditional clear (Default hint)
    ///   * `acquire_pooled(w, h, p, c)`   — wrapper around acquire_unique
    ///   * `acquire_owned(w, h, c)`       — OwnedFB wrapper
    std::unique_ptr<Framebuffer> acquire_unique(int width, int height, bool* out_fresh_alloc = nullptr);

    /// Evict entries until there is room for `incoming_bytes`, or until no
    /// more entries can be evicted.  Returns true if room was made.
    bool evict_lru_for(size_t incoming_bytes);

    /// Evict the oldest entry from a specific size class.
    bool evict_one_from_bucket(FramebufferPoolKey key);

    /// Evict the globally oldest entry across all size classes.
    bool evict_global_lru();

    mutable std::mutex m_mutex;
    FramebufferPoolConfig m_config;
    size_t m_current_bytes{0};
    uint64_t m_tick{0};  // monotonic LRU tick
    std::shared_ptr<chronon3d::FramebufferArena> m_arena;

    // Eviction counters (lifetime)
    std::atomic<size_t> m_evicted_count{0};
    std::atomic<size_t> m_evicted_bytes{0};
    std::atomic<size_t> m_pressure_count{0};

    // Lifetime counters (reset via reset_counters)
    std::atomic<size_t> m_total_allocations{0};
    std::atomic<size_t> m_total_reuses{0};
    std::atomic<size_t> m_total_returns{0};
    std::atomic<size_t> m_total_clears{0};

    std::unordered_map<
        FramebufferPoolKey,
        std::vector<PoolEntry>,
        FramebufferPoolKeyHash
    > m_free;

public:
    // NOTE: alive_token() removed. PoolFbDeleter now uses weak_ptr<FramebufferPool>
    // which is strictly safer against concurrent pool destruction.
};

} // namespace chronon3d::cache
