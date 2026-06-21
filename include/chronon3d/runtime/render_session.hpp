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
// Split into TWO structs defined here, plus a third composition owned
// elsewhere:
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
//
//   Plus a canonical composition outside this header:
//
//   - SoftwareRenderSession    — `RenderSession` + `SoftwareSessionResources`.
//                                 Defined exclusively at
//                                 `<chronon3d/backends/software/software_render_session.hpp>`
//                                 since WP-3 PR 3.4 close-out.  Users that
//                                 need the wrapper struct must include
//                                 that canonical header (the legacy
//                                 duplicate that used to live here was
//                                 removed to eliminate ODR duplication).
//
// GraphExecutor::execute() takes a `RenderSession&` (the engine-generic
// half) so the executor stays backend-agnostic; software-specific session
// resources are only accessed by SoftwareRenderer's own code paths.
// ===========================================================================

#include <memory>

// Engine-generic field includes (acceptable from runtime/).
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/runtime/session_services.hpp>

// WP-8 follow-up — two graph types are forward-declared so the
// runtime/ header can hold accessor *method declarations* whose
// return types reference these engine-internal structs.  Full
// definitions for `graph::SceneHasher` and
// `graph::SceneProgramStore` live in their canonical headers; this
// runtime/ header does NOT pull them in (TICKET-013/017 boundary
// invariant).  Bodies of accessors and of `reset_job()` live in
// the parallel `src/runtime/render_session.cpp`.
namespace chronon3d::graph {
    struct SceneHasher;
    class  SceneProgramStore;
}

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
///
/// PR-5 — `program_store` is stored indirectly (unique_ptr) because
/// SceneProgramStore contains a std::mutex (non-movable).  This keeps
/// RenderSession movable, matching the existing FrameArena pattern.
struct RenderSession {
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    RendererFrameHistory    frame_history;
    RendererDirtyTelemetry  dirty_telemetry;
    RendererLayerHistory    layer_history;
    runtime::SessionServices services;
    // WP-8 follow-up: scene_hasher + program_store relocated to
    // RenderRuntime.  RenderSession now reaches them through the
    // SessionServices pointer bundle that runtime::make_session()
    // populates from runtime.services().  Declared/defined bodies
    // live in src/runtime/render_session.cpp so this header stays
    // free of `render_graph/core/scene_hasher.hpp` and
    // `render_graph/cache/scene_program_store.hpp` includes
    // (TICKET-013 + TICKET-017 boundary invariant).

    /// Per-frame reset (telemetry tracking only).  History preserved.
    void reset_frame_temporaries() {
        dirty_telemetry = RendererDirtyTelemetry{};
    }

    // Convenience accessors that proxy the runtime-owned state via
    // services.  Bodies in src/runtime/render_session.cpp.
    //
    // WP-3 PR 3.0 — the four accessors are intentionally NOT
    // `noexcept`: their bodies in src/runtime/render_session.cpp
    // call `throw std::runtime_error(...)` when the session's
    // `services.scene_hasher` / `services.program_store` pointers
    // are null (default-constructed sessions, test fixtures).
    // A `noexcept` annotation here would invoke `std::terminate`
    // on the unwinding throw; the specifier is now removed so the
    // exception propagates to the caller.  See the test lattice in
    // `tests/runtime/test_render_session_reset_and_isolation.cpp`
    // (`default-constructed scene_hasher() throws instead of
    // terminating` and the equivalent for `program_store()`).
    [[nodiscard]] chronon3d::graph::SceneHasher&       scene_hasher();
    [[nodiscard]] const chronon3d::graph::SceneHasher& scene_hasher() const;
    [[nodiscard]] chronon3d::graph::SceneProgramStore&       program_store();
    [[nodiscard]] const chronon3d::graph::SceneProgramStore& program_store() const;

    /// Arena accessor (still engine-generic; lives on the session).
    [[nodiscard]] FrameArena&       arena()       noexcept { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return *arena_ptr; }

    /// Convenience alias matching the unchanged access pattern: callers
    /// that previously did `m_session.common.scene_hasher()` continue
    /// to work transparently after this WP-8 close-out — the body
    /// now proxies via `services.scene_hasher` (runtime-owned).
    ///
    /// Full per-job reset (telemetry + history + runtime-owned
    /// scene_hasher + program_store via services).  WP-3 collapsed
    /// the legacy shim into this method; WP-8 made the proxy
    /// explicit.  See `docs/refactor-roadmap/03-render-session-boundary.md`.
    void reset_job();
};

// SoftwareRenderSession is intentionally NOT defined here.
// Use: #include <chronon3d/backends/software/software_render_session.hpp>
// for the canonical struct (engine-generic + software-backend composition).
// This header keeps only `RenderSession` so the runtime/ layer does not
// have a struct ODR defined twice across two headers.  See PR 3.1 + 3.4
// in `docs/refactor-roadmap/03-render-session-boundary.md`.

} // namespace chronon3d
