#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <functional>
#include <optional>
#include <memory>
#include <cstddef>
#include <variant>

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
// FramebufferReturnPolicy — explicit variant replacing the implicit priority
// chain of PoolFbDeleter (pool_weak / scratch_slot / owned_by_renderer / scratch_cleanup).
// Full PoolFbDeleter → variant migration deferred to PR-9 (SoftwareRenderer split).
// ---------------------------------------------------------------------------
struct DeleteFramebuffer {};
struct ReturnToPool { std::weak_ptr<cache::FramebufferPool> pool; };
struct ReturnToScratch { Framebuffer** slot{nullptr}; };
struct RestoreScratchHandle { std::function<void()> cleanup; };
struct RendererOwned {};

using FramebufferReturnPolicy = std::variant<
    DeleteFramebuffer,
    ReturnToPool,
    ReturnToScratch,
    RestoreScratchHandle,
    RendererOwned
>;

// ---------------------------------------------------------------------------
// PoolFbDeleter — custom deleter that returns a Framebuffer to its pool.
//
// Uses weak_ptr<FramebufferPool> instead of a raw pointer + alive token.
// This is safe against concurrent pool destruction: if the pool is gone,
// the weak_ptr::lock() fails and the framebuffer is simply deleted.
// ---------------------------------------------------------------------------
struct PoolFbDeleter {
    std::weak_ptr<cache::FramebufferPool> pool_weak;
    /// When set, the FB is returned to this slot (cleared) instead of
    /// released to the pool.  Used by the TransformNode scratch buffer
    /// to keep a persistent buffer across frames without acquire/recycle.
    Framebuffer** scratch_slot{nullptr};
    /// When true, the FB is owned permanently by the renderer (e.g., a
    /// ping-pong buffer).  The deleter does nothing — no pool release,
    /// no scratch restore, no delete.  The renderer manages lifetime
    /// explicitly via RendererBufferRing::reset().
    bool owned_by_renderer{false};
    /// When set, the FB is borrowed from a TransformScratchBuffer via its
    /// RAII Handle, captured inside this std::function as a lambda.
    /// Calling scratch_cleanup() or replacing/clearing it destroys the
    /// lambda (and with it the Handle), which restores the FB to the
    /// owner's storage.  Takes precedence over pool_weak / scratch_slot /
    /// owned_by_renderer when set.
    std::function<void()> scratch_cleanup{};
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
