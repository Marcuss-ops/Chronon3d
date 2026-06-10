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
    ~TransformScratchBuffer() { reset(); }

    TransformScratchBuffer(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer& operator=(const TransformScratchBuffer&) = delete;
    TransformScratchBuffer(TransformScratchBuffer&& other) noexcept;
    TransformScratchBuffer& operator=(TransformScratchBuffer&& other) noexcept;

    /// Lazily allocate/reallocate at the requested size (rounded to bucket).
    /// Returns a stable pointer across frames once created.
    [[nodiscard]] Framebuffer* ensure_size(int width, int height);

    /// The scratch framebuffer pointer.
    [[nodiscard]] Framebuffer* fb() const noexcept { return m_scratch; }

    /// Address-of the scratch slot pointer, so the deleter can store
    /// the cleared FB back into it.
    [[nodiscard]] Framebuffer** slot() noexcept { return &m_scratch; }

    /// Convenience: ensure_size + return a FramebufferSlotView bundling
    /// both the scratch FB and its restore slot, for wiring into
    /// RenderScratchContext.
    [[nodiscard]] graph::FramebufferSlotView slot_view(int width, int height) {
        Framebuffer* fb_ptr = ensure_size(width, height);
        return {fb_ptr, slot()};
    }

    /// Deallocate the scratch.
    void reset();

private:
    Framebuffer* m_scratch{nullptr};
};

} // namespace chronon3d
