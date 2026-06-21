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
//
// The FramebufferReturnPolicy variant is the single source of truth.
// All callers use policy assignment directly (e.g. deleter.policy = RendererOwned{}).
// ---------------------------------------------------------------------------
struct PoolFbDeleter {
    FramebufferReturnPolicy policy{DeleteFramebuffer{}};

    // ── Convenience constructors ─────────────────────────────────────

    PoolFbDeleter() = default;

    /*implicit*/ PoolFbDeleter(std::weak_ptr<cache::FramebufferPool> pool)
        : policy(ReturnToPool{std::move(pool)}) {}

    /*implicit*/ PoolFbDeleter(FramebufferReturnPolicy p)
        : policy(std::move(p)) {}

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

// ── TICKET-011-final ──────────────────────────────────────────────────────────
// make_owned_fb_from_shared — free helper replacing the missing
// `OwnedFB::from_shared_no_pool` static factory.  OwnedFB is a
// unique_ptr<T, PoolFbDeleter> type alias; static methods cannot be
// added to type aliases, so we expose this factory inline.
inline OwnedFB make_owned_fb_from_shared(std::shared_ptr<Framebuffer>&& src) noexcept {
    if (!src) return OwnedFB{};
    // std::shared_ptr has no .release(); copy-construct a fresh OwnedFB
    // that is decoupled from the source's lifetime.
    return OwnedFB(new Framebuffer(*src), PoolFbDeleter(DeleteFramebuffer{}));
}

// ---------------------------------------------------------------------------
// Helper: borrow a CachedFB as a FramebufferRef + keep the shared_ptr alive.
// ---------------------------------------------------------------------------
struct BorrowedCachedFB {
    FramebufferRef ref;
    CachedFB       keep_alive;
};

} // namespace chronon3d
