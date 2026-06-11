#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_slot_view.hpp>
#include <memory>

namespace chronon3d::cache {
class FramebufferPool;
}

namespace chronon3d {

/**
 * RendererBufferRing — RAII owner of the renderer's ping-pong framebuffers.
 *
 * Two persistent Framebuffers are owned as raw pointers (they are never
 * handed to the pool).  The read/write indices swap after each frame so
 * that ClearNode always writes to an exclusive buffer while the encoder
 * thread reads the previous one.  m_prev_framebuffer is a shared_ptr
 * wrapper around the current read ping for COW / early-exit reuse.
 */
class RendererBufferRing {
public:
    RendererBufferRing() = default;
    ~RendererBufferRing() { reset(); }

    RendererBufferRing(const RendererBufferRing&) = delete;
    RendererBufferRing& operator=(const RendererBufferRing&) = delete;
    RendererBufferRing(RendererBufferRing&& other) noexcept;
    RendererBufferRing& operator=(RendererBufferRing&& other) noexcept;

    /// Allocate/reallocate both ping buffers to match canvas size.
    /// @param pool Optional FramebufferPool to acquire from (and return to).
    ///             When null, raw new/delete is used as fallback.
    void ensure_size(int width, int height,
                     cache::FramebufferPool* pool = nullptr);

    /// Swap read/write indices after a frame completes.
    /// Also repoints prev_framebuffer() to the new read ping.
    void swap();

    /// The write ping — exclusive, no COW needed.
    [[nodiscard]] Framebuffer* write_fb() const noexcept { return m_ping_fb[m_ping_write_idx]; }

    /// Address-of the write slot for the PoolFbDeleter restore path.
    [[nodiscard]] Framebuffer** write_slot() noexcept { return &m_ping_fb[m_ping_write_idx]; }

    /// Convenience: returns a FramebufferSlotView bundling both write ping
    /// and its restore slot, for wiring into RenderScratchContext.
    [[nodiscard]] graph::FramebufferSlotView write_slot_view() noexcept {
        return {write_fb(), write_slot()};
    }

    /// The read ping — the "previous" completed frame.
    [[nodiscard]] Framebuffer* read_fb() const noexcept { return m_ping_fb[m_ping_read_idx]; }

    /// Access a specific ping by index (0 or 1). Used for pointer comparisons.
    [[nodiscard]] Framebuffer* ping_fb(int idx) const noexcept;

    /// The "previous frame" framebuffer (shared_ptr for COW / early-exit paths).
    [[nodiscard]] std::shared_ptr<Framebuffer>& prev_framebuffer() noexcept { return m_prev_framebuffer; }
    [[nodiscard]] const std::shared_ptr<Framebuffer>& prev_framebuffer() const noexcept { return m_prev_framebuffer; }

    /// Deallocate all buffers and reset indices.
    void reset();

private:
    Framebuffer* m_ping_fb[2]{nullptr, nullptr};
    int m_ping_read_idx{0};
    int m_ping_write_idx{1};
    cache::FramebufferPool* m_pool{nullptr};
    std::shared_ptr<Framebuffer> m_prev_framebuffer;
};

} // namespace chronon3d
