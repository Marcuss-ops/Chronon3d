#pragma once

// ---------------------------------------------------------------------------
// framebuffer_slot_view.hpp
//
// Lightweight view bundling a raw Framebuffer* and its restore slot
// (Framebuffer**).  Used by RenderScratchContext so that transform scratch
// and ping-pong write pointers travel together as a single logical unit.
//
// The slot is consumed by PoolFbDeleter to restore the buffer when the
// borrowing OwnedFB is released.
//
// Extracted from render_graph_context.hpp so that backends
// (buffer_ring.hpp, scratch_buffer.hpp) don't need to pull in the entire
// render-graph header just for this two-member struct.
// ---------------------------------------------------------------------------

namespace chronon3d {
    class Framebuffer;
}

namespace chronon3d::graph {

struct FramebufferSlotView {
    Framebuffer*  fb{nullptr};
    Framebuffer** slot{nullptr};

    explicit operator bool() const noexcept {
        return fb != nullptr && slot != nullptr;
    }
};

} // namespace chronon3d::graph
