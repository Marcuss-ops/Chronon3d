#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_slot_view.hpp>

namespace chronon3d {

/**
 * TransformScratchBuffer — RAII owner of the renderer's transform scratch buffer.
 *
 * A persistent framebuffer reused by TransformNode across frames.
 * Eliminates pool bucket misses when transform output size varies by a
 * few pixels per frame (animated transforms).
 */
class TransformScratchBuffer {
public:
    TransformScratchBuffer() = default;
    // Defaulted destructor: `m_scratch_owner` (std::unique_ptr<Framebuffer>)
    // RAII-cleans the Framebuffer.  `m_scratch_ptr` is an aliased raw
    // pointer — no separate release needed.
    ~TransformScratchBuffer() = default;

    TransformScratchBuffer(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer& operator=(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer(TransformScratchBuffer&& other) noexcept;
    TransformScratchBuffer& operator=(TransformScratchBuffer&& other) noexcept;

    /// Lazily allocate/reallocate at the requested size (rounded to bucket).
    /// Returns a stable pointer across frames once created.
    [[nodiscard]] Framebuffer* ensure_size(int width, int height);

    /// The scratch framebuffer pointer.
    [[nodiscard]] Framebuffer* fb() const noexcept { return m_scratch_ptr; }

    /// Address-of the scratch slot pointer, so the deleter can store
    /// the cleared FB back into it.
    [[nodiscard]] Framebuffer** slot() noexcept { return &m_scratch_ptr; }

    /// Convenience: ensure_size + return a FramebufferSlotView bundling
    /// both the scratch FB and its restore slot, for wiring into
    /// RenderScratchContext.
    [[nodiscard]] graph::FramebufferSlotView slot_view(int width, int height) {
        Framebuffer* fb_ptr = ensure_size(width, height);
        return {fb_ptr, slot()};
    }

    /// Deallocate the scratch.  Called by `SoftwareSessionResources::reset_job()`
    /// (full per-job reset) and by the destructor.  NOT called from
    /// `reset_frame_temporaries()` — see that comment for the rationale.
    void reset();

private:
    // Dual-pointer ownership: `m_scratch_owner` holds the Framebuffer
    // via std::unique_ptr (RAII cleans up on destruction / move), while
    // `m_scratch_ptr` mirrors `m_scratch_owner.get()` so that
    // `slot()` can return `Framebuffer**` for the ReturnToScratch
    // deleter's restore-back path.  Keeping the raw pointer parallel
    // lets callers still receive a `Framebuffer*` from `fb()`/`slot_view()`
    // while ownership stays strictly in the unique_ptr.
    std::unique_ptr<Framebuffer> m_scratch_owner;
    Framebuffer* m_scratch_ptr{nullptr};
};

} // namespace chronon3d
