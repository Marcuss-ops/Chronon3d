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
// The policy variant is the single source of truth.  The legacy fields
// (pool_weak, scratch_slot, owned_by_renderer, scratch_cleanup) are
// deprecated accessors that read/write the variant for backward compat.
// ---------------------------------------------------------------------------
struct PoolFbDeleter {
    FramebufferReturnPolicy policy{DeleteFramebuffer{}};

    // ── Backward-compat deprecated accessors ──────────────────────────

    [[deprecated("Use policy = ReturnToPool{pool} instead")]]
    std::weak_ptr<cache::FramebufferPool>& pool_weak() {
        if (!std::holds_alternative<ReturnToPool>(policy))
            policy.emplace<ReturnToPool>();
        return std::get<ReturnToPool>(policy).pool;
    }
    [[deprecated("Use policy = ReturnToScratch{slot} instead")]]
    Framebuffer**& scratch_slot() {
        if (!std::holds_alternative<ReturnToScratch>(policy))
            policy.emplace<ReturnToScratch>();
        return std::get<ReturnToScratch>(policy).slot;
    }
    [[deprecated("Use policy = RendererOwned{} instead")]]
    bool& owned_by_renderer() {
        static bool dummy = false;
        if (std::holds_alternative<RendererOwned>(policy)) return dummy;
        policy.emplace<RendererOwned>();
        return dummy;
    }
    [[deprecated("Use policy = RestoreScratchHandle{cleanup} instead")]]
    std::function<void()>& scratch_cleanup() {
        if (!std::holds_alternative<RestoreScratchHandle>(policy))
            policy.emplace<RestoreScratchHandle>();
        return std::get<RestoreScratchHandle>(policy).cleanup;
    }

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

// ---------------------------------------------------------------------------
// Helper: borrow a CachedFB as a FramebufferRef + keep the shared_ptr alive.
// ---------------------------------------------------------------------------
struct BorrowedCachedFB {
    FramebufferRef ref;
    CachedFB       keep_alive;
};

} // namespace chronon3d
