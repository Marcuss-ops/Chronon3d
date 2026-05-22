#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

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
// FramebufferPoolStats
// ---------------------------------------------------------------------------
struct FramebufferPoolStats {
    size_t current_bytes{0};
    size_t available_count{0};
    size_t max_bytes{0};
};

// ---------------------------------------------------------------------------
// FramebufferPool
// ---------------------------------------------------------------------------
class FramebufferPool : public std::enable_shared_from_this<FramebufferPool> {
public:
    explicit FramebufferPool(size_t max_bytes = 64ULL * 1024ULL * 1024ULL);

    /// Acquire a framebuffer of the requested size.
    /// The pool never clears memory; callers own clear semantics.
    std::shared_ptr<Framebuffer> acquire(int width, int height, bool clear = true);

    /// Acquire a framebuffer that automatically releases itself back to the pool upon destruction.
    std::shared_ptr<Framebuffer> acquire_pooled(int width, int height, std::shared_ptr<FramebufferPool> pool, bool clear = true);

    void release(Framebuffer* fb);

    /// Release all pooled framebuffers.
    void clear();

    /// Preallocate framebuffers of a given size into the pool.
    /// Returns the number of framebuffers actually created (may be less than
    /// requested if the pool size limit is reached).
    size_t preallocate(const FramebufferPoolPreallocOptions& options);

    [[nodiscard]] size_t current_bytes() const;
    [[nodiscard]] size_t available_count() const;
    [[nodiscard]] size_t max_bytes() const;
    [[nodiscard]] FramebufferPoolStats stats() const;

private:
    std::unique_ptr<Framebuffer> acquire_unique(int width, int height, bool clear = true);

    mutable std::mutex m_mutex;
    size_t m_max_bytes;
    size_t m_current_bytes{0};

    std::unordered_map<
        FramebufferPoolKey,
        std::vector<std::unique_ptr<Framebuffer>>,
        FramebufferPoolKeyHash
    > m_free;
};

} // namespace chronon3d::cache
