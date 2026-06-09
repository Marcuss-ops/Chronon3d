#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <memory>
#include <cstddef>

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d {

// ---------------------------------------------------------------------------
// FramebufferRef — non-owning view of a Framebuffer
//
// Trivially copyable, no atomic overhead. Use in hot-path function signatures
// (node execute(), compositor, etc.) where shared_ptr refcounting is wasteful.
// ---------------------------------------------------------------------------
class FramebufferRef {
public:
    FramebufferRef() = default;
    /*implicit*/ FramebufferRef(std::nullptr_t) {}
    /*implicit*/ FramebufferRef(Framebuffer& fb) : m_ptr(&fb) {}
    /*implicit*/ FramebufferRef(Framebuffer* fb) : m_ptr(fb) {}

    Framebuffer& operator*() const { return *m_ptr; }
    Framebuffer* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    [[nodiscard]] Framebuffer* get() const { return m_ptr; }
    [[nodiscard]] Framebuffer* release() { auto* p = m_ptr; m_ptr = nullptr; return p; }
    void reset(Framebuffer* p = nullptr) { m_ptr = p; }

    bool operator==(const FramebufferRef& other) const { return m_ptr == other.m_ptr; }
    bool operator!=(const FramebufferRef& other) const { return m_ptr != other.m_ptr; }

private:
    Framebuffer* m_ptr{nullptr};
};

// ---------------------------------------------------------------------------
// PoolFbDeleter — custom deleter that returns a Framebuffer to its pool.
// ---------------------------------------------------------------------------
struct PoolFbDeleter {
    cache::FramebufferPool* pool{nullptr};
    std::weak_ptr<bool> pool_alive;
    /// When set, the FB is returned to this slot (cleared) instead of
    /// released to the pool.  Used by the TransformNode scratch buffer
    /// to keep a persistent buffer across frames without acquire/recycle.
    Framebuffer** scratch_slot{nullptr};
    /// When true, the FB is owned permanently by the renderer (e.g., a
    /// ping-pong buffer).  The deleter does nothing — no pool release,
    /// no scratch restore, no delete.  The renderer manages lifetime
    /// explicitly via m_ping_fb[] / clear_caches().
    bool owned_by_renderer{false};
    void operator()(Framebuffer* fb) const noexcept;
};

// ---------------------------------------------------------------------------
// OwnedFB — unique_ptr<Framebuffer, PoolFbDeleter>
//
// Move-only, single-owner framebuffer handle. Automatically returns the
// framebuffer to its pool on destruction. Zero atomic overhead (unlike
// shared_ptr). Use in the executor's per-frame storage and node execute()
// return values.
// ---------------------------------------------------------------------------
using OwnedFB = std::unique_ptr<Framebuffer, PoolFbDeleter>;

// ---------------------------------------------------------------------------
// CachedFB — shared_ptr<Framebuffer> alias for cache-stored framebuffers.
// ---------------------------------------------------------------------------
using CachedFB = std::shared_ptr<Framebuffer>;

// ---------------------------------------------------------------------------
// Helper: borrow a CachedFB as a FramebufferRef + keep the shared_ptr alive.
// ---------------------------------------------------------------------------
struct BorrowedCachedFB {
    FramebufferRef ref;
    CachedFB       keep_alive;
};

} // namespace chronon3d
