#pragma once

// ---------------------------------------------------------------------------
// runtime/render_session.hpp
//
// TICKET-008 — Per-session rendering state, relocated from
// `include/chronon3d/core/memory/render_session.hpp` to resolve a
// dependency-direction violation.  The previous location in `core/memory/`
// pulled in software-specific headers (`backends/software/buffer_ring.hpp`,
// `backends/software/scratch_buffer.hpp`) and render-graph internals
// (`render_graph/core/scene_hasher.hpp`) — a `core/` header must never
// depend on a backend.
//
// Split into three structs:
//
//   - RenderSession            — engine-generic per-session state that any
//                                 RenderBackend implementation can consume
//                                 (FrameArena, frame history, dirty
//                                 telemetry, layer-bbox history, scene
//                                 hasher).
//   - SoftwareSessionResources — software-specific session resources
//                                 (ping-pong buffer ring, transform
//                                 scratch).  Lives in `backends/software/`
//                                 semantically; declared here only as a
//                                 convenience wrapper for SoftwareRenderer.
//   - SoftwareRenderSession    — composition of the two above, owned by
//                                 SoftwareRenderer as `m_session`.
//
// GraphExecutor::execute() takes a `RenderSession&` (the engine-generic
// half) so the executor stays backend-agnostic; software-specific session
// resources are only accessed by SoftwareRenderer's own code paths.
// ===========================================================================

#include <memory>

// Engine-generic field includes (acceptable from runtime/).
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>

// Software-specific field includes (TICKET-008 note: lives in runtime/ for
// tuple ergonomics; the runtime/ layer is intentional here because
// `SoftwareRenderSession` is a composition owned by SoftwareRenderer, not a
// dependency of the runtime/ layer on the backend — see TICKET-009 for the
// next-step caching separation that will move these into RenderRuntime).
#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>

namespace chronon3d {

/// Engine-generic per-session rendering state.
///
/// All members are default-constructible.  FrameArena is stored indirectly
/// because it contains a std::pmr::monotonic_buffer_resource which is
/// non-movable; the unique_ptr keeps the outer struct movable.
struct RenderSession {
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    RendererFrameHistory    frame_history;
    RendererDirtyTelemetry  dirty_telemetry;
    RendererLayerHistory    layer_history;
    graph::SceneHasher      scene_hasher;

    /// Convenience accessor for the frame arena.
    [[nodiscard]] FrameArena& arena() noexcept { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return *arena_ptr; }

    /// Clear per-frame state for reuse between unrelated render sessions.
    /// Per-frame resources (arena memory, buffer ring, scratch) are NOT
    /// cleared here — those live on the backend-specific resources struct
    /// and have their own lifetime policy.
    void clear_per_frame() {
        frame_history   = RendererFrameHistory{};
        dirty_telemetry = RendererDirtyTelemetry{};
        layer_history   = RendererLayerHistory{};
        scene_hasher    = graph::SceneHasher{};
    }
};

/// Software-specific per-session resources.
///
/// Owned by SoftwareRenderer; not visible to engine-generic code paths.
struct SoftwareSessionResources {
    RendererBufferRing      buffer_ring;
    TransformScratchBuffer  scratch_buffer;

    void clear_per_frame() {
        buffer_ring.reset();
        scratch_buffer.reset();
    }
};

/// Composition of engine-generic + software-specific session state.
///
/// SoftwareRenderer holds one of these as `m_session`; the public accessors
/// `session()` return the engine-generic half and `software_session()` the
/// full composition, so callers that only need the engine-generic half
/// (e.g. GraphExecutor) keep working without pulling in software internals.
struct SoftwareRenderSession {
    RenderSession            common;
    SoftwareSessionResources software;

    void clear_per_frame() {
        common.clear_per_frame();
        software.clear_per_frame();
    }
};

} // namespace chronon3d
