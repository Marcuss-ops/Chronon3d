#pragma once

// ── TransformScratchBuffer ────────────────────────────────────────────────────
//
// Encapsulates the persistent transform scratch framebuffer used by
// SoftwareRenderer.  TransformNode reuses this buffer across frames instead
// of going through the pool, eliminating bucket misses when animated
// transforms vary output size by a few pixels per frame.
//
// The class owns the Framebuffer memory outright (via std::unique_ptr) — no
// raw pointer + PoolFbDeleter dance, no lifetime ambiguity.
//
// ── Usage ────────────────────────────────────────────────────────────────────
//   TransformScratchBuffer scratch;
//   scratch.ensure_capacity(width, height);  // called at frame start
//   Framebuffer* fb = scratch.acquire();      // get pointer, marks "in use"
//   // The PoolFbDeleter-style ownership transfer is handled by
//   // release_handle() — see RenderGraphContext for the integration point.
//
// ── Reset semantics ──────────────────────────────────────────────────────────
//   reset() releases the storage.  After reset() the buffer is empty and
//   must be re-initialized with ensure_capacity() before next use.

#include <chronon3d/core/memory/framebuffer.hpp>
#include <memory>

namespace chronon3d {

class TransformScratchBuffer {
public:
    TransformScratchBuffer() = default;
    ~TransformScratchBuffer() = default;

    // Non-copyable.  Movable: the unique_ptr transfers ownership and the
    // raw m_storage pointer is updated to point at the new unique_ptr's
    // get().  Any outstanding Handle that aliases the *old* raw pointer
    // becomes dangling after the move — callers must not hold a Handle
    // across a move of the owning TransformScratchBuffer.
    TransformScratchBuffer(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer& operator=(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer(TransformScratchBuffer&& other) noexcept;
    TransformScratchBuffer& operator=(TransformScratchBuffer&& other) noexcept;

    /// Allocate or reallocate the scratch buffer to (at least) the requested
    /// size.  Existing buffer is reused if it's large enough; otherwise
    /// the old storage is released and a fresh buffer is allocated.
    void ensure_capacity(int width, int height);

    /// Returns the raw Framebuffer pointer for the scratch slot.  The caller
    /// can use this pointer directly while the buffer is in use; ownership
    /// stays with the TransformScratchBuffer.
    [[nodiscard]] Framebuffer* acquire();

    /// Returns a "handle" — a small RAII wrapper that owns the scratch
    /// pointer while alive.  When the handle is destroyed, the scratch
    /// pointer is restored to the TransformScratchBuffer (i.e. the buffer
    /// becomes available again).  This mirrors the old PoolFbDeleter
    /// scratch_slot pattern but with std::unique_ptr semantics.
    class Handle {
    public:
        Handle() = default;
        Handle(TransformScratchBuffer* owner, Framebuffer* fb) : m_owner(owner), m_fb(fb) {}
        Handle(Handle&& other) noexcept : m_owner(other.m_owner), m_fb(other.m_fb) {
            other.m_owner = nullptr;
            other.m_fb = nullptr;
        }
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                release();
                m_owner = other.m_owner;
                m_fb = other.m_fb;
                other.m_owner = nullptr;
                other.m_fb = nullptr;
            }
            return *this;
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        ~Handle() { release(); }

        [[nodiscard]] Framebuffer* get() const { return m_fb; }
        [[nodiscard]] Framebuffer* operator->() const { return m_fb; }
        [[nodiscard]] explicit operator bool() const { return m_fb != nullptr; }

        /// Release ownership back to the owner without destroying storage.
        Framebuffer* detach();

    private:
        void release();
        TransformScratchBuffer* m_owner{nullptr};
        Framebuffer* m_fb{nullptr};
    };

    /// Acquire a RAII handle.  After this call, the scratch pointer is
    /// detached from the TransformScratchBuffer until the handle is
    /// destroyed or detach()ed.
    [[nodiscard]] Handle acquire_handle();

    /// Direct write access to the storage slot pointer.  Used by
    /// RenderGraphContext's PoolFbDeleter-style restoration path, where
    /// the deleter needs to write back the FB* into the slot.
    [[nodiscard]] Framebuffer** slot() { return reinterpret_cast<Framebuffer**>(&m_storage); }

    /// True if the scratch has been allocated.
    [[nodiscard]] bool is_allocated() const { return m_storage != nullptr; }

    /// Direct read access to the underlying storage (for the const path).
    [[nodiscard]] const Framebuffer* storage() const { return m_storage; }

    /// Release the scratch storage (e.g. on resolution change or shutdown).
    void reset();

private:
    // Owned storage.  We store a raw pointer + unique_ptr separately so that
    // the scratch_slot can write back the Framebuffer* into m_storage while
    // ownership stays in m_owner.  This mirrors the old PoolFbDeleter
    // scratch_slot pattern but is wrapped in a clean RAII type.
    Framebuffer* m_storage{nullptr};
    std::unique_ptr<Framebuffer> m_owner;
};

} // namespace chronon3d
