#pragma once

#include <chronon3d/core/config.hpp>
#include <chronon3d/assets/asset_registry.hpp>

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
#include <chronon3d/runtime/session_services.hpp>

// Software-specific field includes (TICKET-008 note: lives in runtime/ for
// tuple ergonomics; the runtime/ layer is intentional here because
// `SoftwareRenderSession` is a composition owned by SoftwareRenderer, not a
// dependency of the runtime/ layer on the backend — see TICKET-009 for the
// next-step caching separation that will move these into RenderRuntime).
//
// TICKET-011-final consolidation: the canonical definition of
// `SoftwareSessionResources` lives in this single, semantically-correct
// header (`backends/software/software_session_resources.hpp`).  The
// previous stale 2-field inline duplicate in this file caused a
// C++-level struct redefinition error; this include brings the
// canonical 3-field struct in.  `SoftwareRenderSession::clear_per_frame`
// below now calls `reset_job()` to preserve the old behaviour (resets
// buffer_ring + scratch_buffer).
#include <chronon3d/backends/software/software_session_resources.hpp>

namespace chronon3d {

/// Engine-generic per-session rendering state.
///
/// All members are default-constructible.  FrameArena is stored indirectly
/// because it contains a std::pmr::monotonic_buffer_resource which is
/// non-movable; the unique_ptr keeps the outer struct movable.
///
/// TICKET-011a follow-up #1 — the `services` field is a non-owning
/// back-pointer bundle populated by `runtime::make_session()` so
/// session-aware contexts (currently GraphExecutor callers) can
/// read registries / caches / pools / default_assets_root through
/// the session itself instead of reaching a process-global.
struct RenderSession {
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    RendererFrameHistory    frame_history;
    RendererDirtyTelemetry  dirty_telemetry;
    RendererLayerHistory    layer_history;
    graph::SceneHasher      scene_hasher;
    runtime::SessionServices services;

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
        // TICKET-011-final: `SoftwareSessionResources` (canonical, from
        // include/chronon3d/backends/software/software_session_resources.hpp)
        // exposes `reset_job()` and `reset_frame_temporaries()`.  Call the
        // full-reset variant here because the legacy `clear_per_frame()`
        // implementation lived on the stale duplicate struct and reset both
        // buffer_ring + scratch_buffer; `reset_job()` mirrors that exactly.
        software.reset_job();
    }
};

} // namespace chronon3d
