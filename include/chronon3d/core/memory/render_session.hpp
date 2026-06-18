#pragma once

// ---------------------------------------------------------------------------
// render_session.hpp
//
// RenderSession groups all per-session state that was previously split across
// GraphExecutor (m_frame_arena) and SoftwareRenderer (RendererFrameState).
//
// A RenderSession represents a continuous rendering context (e.g., frames
// 0..N of a video export).  It owns:
//   - FrameArena         — memory arena for temporary allocations
//   - RendererBufferRing — ping-pong framebuffers for incremental reuse
//   - TransformScratchBuffer — transform scratch buffer
//   - RendererFrameHistory   — previous frame's camera + fingerprint
//   - RendererDirtyTelemetry — dirty-rect telemetry counters
//   - RendererLayerHistory   — per-layer bbox history for dirty-rect diffing
//   - SceneHasher            — scene fingerprinting
//
// FrameArena is stored indirectly (unique_ptr) because it contains a
// std::pmr::monotonic_buffer_resource which is non-movable.  The unique_ptr
// makes RenderSession movable — required by SoftwareRenderer's defaulted
// move constructor.
//
// SoftwareRenderer replaces its RendererFrameState m_frame_state with a
// RenderSession, and GraphExecutor uses session.arena() instead of
// its own m_frame_arena — making GraphExecutor nearly stateless.
// ===========================================================================

#include <memory>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>

namespace chronon3d {

/// Per-session rendering state.
///
/// All members are default-constructible and movable.  Create one
/// RenderSession per rendering context (e.g., one per SoftwareRenderer
/// instance, or one per concurrent render job).
struct RenderSession {
    // Arena is behind unique_ptr because FrameArena contains a
    // std::pmr::monotonic_buffer_resource which is non-movable.
    // All other members are plain data and remain direct members.
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    RendererBufferRing      buffer_ring;
    TransformScratchBuffer  scratch_buffer;
    RendererFrameHistory    frame_history;
    RendererDirtyTelemetry  dirty_telemetry;
    RendererLayerHistory    layer_history;
    graph::SceneHasher      scene_hasher;

    /// Convenience accessor for the frame arena.
    [[nodiscard]] FrameArena& arena() { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const { return *arena_ptr; }

    /// Clear per-frame state for reuse between unrelated render sessions.
    void clear_per_frame() {
        buffer_ring.reset();
        scratch_buffer.reset();
        frame_history = RendererFrameHistory{};
        dirty_telemetry = RendererDirtyTelemetry{};
        layer_history = RendererLayerHistory{};
        scene_hasher = graph::SceneHasher{};
        // arena is intentionally NOT cleared here — its memory is managed
        // by the ArenaGuard in the executor, not by session-level clear.
    }
};

} // namespace chronon3d
