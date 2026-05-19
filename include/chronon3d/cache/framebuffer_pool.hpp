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
// FramebufferPool
// ---------------------------------------------------------------------------
class FramebufferPool {
public:
    explicit FramebufferPool(size_t max_bytes = 512ULL * 1024ULL * 1024ULL);

    /// Acquire a cleared framebuffer of the requested size.
    /// If a compatible framebuffer exists in the pool, it is reused.
    std::shared_ptr<Framebuffer> acquire(int width, int height);

    /// Return a framebuffer to the pool for reuse.
    /// If the pool has reached max_bytes, the framebuffer is dropped.
    void release(std::shared_ptr<Framebuffer> fb);

    /// Release all pooled framebuffers.
    void clear();

    [[nodiscard]] size_t current_bytes() const;
    [[nodiscard]] size_t available_count() const;

private:
    mutable std::mutex m_mutex;
    size_t m_max_bytes;
    size_t m_current_bytes{0};

    std::unordered_map<
        FramebufferPoolKey,
        std::vector<std::shared_ptr<Framebuffer>>,
        FramebufferPoolKeyHash
    > m_free;
};

// ---------------------------------------------------------------------------
// PooledFramebuffer — RAII handle that auto-releases to pool on destruction
// ---------------------------------------------------------------------------
class PooledFramebuffer {
public:
    PooledFramebuffer() = default;

    PooledFramebuffer(std::shared_ptr<Framebuffer> fb, FramebufferPool* pool)
        : m_fb(std::move(fb)), m_pool(pool) {}

    ~PooledFramebuffer() {
        if (m_pool && m_fb) {
            m_pool->release(std::move(m_fb));
        }
    }

    // Move-only
    PooledFramebuffer(PooledFramebuffer&& other) noexcept
        : m_fb(std::move(other.m_fb)), m_pool(other.m_pool) {
        other.m_pool = nullptr;
    }

    PooledFramebuffer& operator=(PooledFramebuffer&& other) noexcept {
        if (this != &other) {
            if (m_pool && m_fb) {
                m_pool->release(std::move(m_fb));
            }
            m_fb = std::move(other.m_fb);
            m_pool = other.m_pool;
            other.m_pool = nullptr;
        }
        return *this;
    }

    // No copying
    PooledFramebuffer(const PooledFramebuffer&) = delete;
    PooledFramebuffer& operator=(const PooledFramebuffer&) = delete;

    [[nodiscard]] Framebuffer* get() const { return m_fb.get(); }
    Framebuffer& operator*() const { return *m_fb; }
    Framebuffer* operator->() const { return m_fb.get(); }

    [[nodiscard]] bool valid() const { return m_fb != nullptr; }

    /// Detach the framebuffer from pool management (ownership transfers to caller).
    std::shared_ptr<Framebuffer> detach() {
        m_pool = nullptr;
        return std::move(m_fb);
    }

private:
    std::shared_ptr<Framebuffer> m_fb;
    FramebufferPool* m_pool{nullptr};
};

} // namespace chronon3d::cache
