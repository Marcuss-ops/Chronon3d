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
};

// ---------------------------------------------------------------------------
// FramebufferPool
// ---------------------------------------------------------------------------
class FramebufferPool : public std::enable_shared_from_this<FramebufferPool> {
public:
    explicit FramebufferPool(size_t max_bytes = 8192ULL * 1024ULL * 1024ULL);
    ~FramebufferPool();

    /// Set a static arena to be used for new allocations.
    void set_arena(std::shared_ptr<chronon3d::FramebufferArena> arena);

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

    /// Acquire an OwnedFB without attaching a deleter to a shared pool handle.
    /// The framebuffer is immediately owned by the returned OwnedFB and will be
    /// returned to this pool on destruction.
    OwnedFB acquire_owned_raw(int width, int height, bool clear = true);

    void release(Framebuffer* fb);

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
    std::unique_ptr<Framebuffer> acquire_unique(int width, int height, bool* out_fresh_alloc = nullptr);

    mutable std::mutex m_mutex;
    size_t m_max_bytes;
    size_t m_current_bytes{0};
    std::shared_ptr<chronon3d::FramebufferArena> m_arena;

    // Lifetime counters (reset via reset_counters)
    std::atomic<size_t> m_total_allocations{0};
    std::atomic<size_t> m_total_reuses{0};
    std::atomic<size_t> m_total_returns{0};
    std::atomic<size_t> m_total_clears{0};

    std::unordered_map<
        FramebufferPoolKey,
        std::vector<std::unique_ptr<Framebuffer>>,
        FramebufferPoolKeyHash
    > m_free;

    std::shared_ptr<bool> m_alive;
public:
    [[nodiscard]] std::shared_ptr<bool> alive_token() const { return m_alive; }
};

} // namespace chronon3d::cache
