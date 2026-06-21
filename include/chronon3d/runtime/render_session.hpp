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
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/cache/scene_program_store.hpp>
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
// canonical 3-field struct in.  `SoftwareSessionResources::reset_job()`
// is the canonical full-reset path.
//
// `SoftwareRenderSession` (the composition wrapper) is NOT defined in
// this header — it lives canonically at
// `<chronon3d/backends/software/software_render_session.hpp>` so there
// is exactly ONE definition of the struct across the codebase (no ODR
// risk from re-inclusion; WP-3 close-out removed the legacy duplicate
// that used to live here).
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
///
/// PR-5 — `program_store` is stored indirectly (unique_ptr) because
/// SceneProgramStore contains a std::mutex (non-movable).  This keeps
/// RenderSession movable, matching the existing FrameArena pattern.
struct RenderSession {
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    RendererFrameHistory    frame_history;
    RendererDirtyTelemetry  dirty_telemetry;
    RendererLayerHistory    layer_history;
    graph::SceneHasher      scene_hasher;
    runtime::SessionServices services;
    std::unique_ptr<graph::SceneProgramStore> program_store{
        std::make_unique<graph::SceneProgramStore>()};

    /// Convenience accessor for the frame arena.
    [[nodiscard]] FrameArena& arena() noexcept { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return *arena_ptr; }

    // ── Work Package 3 — explicit reset semantics ────────────────────
    //
    // `reset_frame_temporaries()` clears ONLY frame-scoped telemetry
    // tracking (DirtyHistory counters).  This is what callers should
    // invoke at the start of every new frame for a job that is being
    // continued across frames; it preserves history (frame scene
    // fingerprints, layer bbox history, scene-hasher state) so the
    // next frame's diff logic keeps working.
    //
    // `reset_job()` is a FULL reset: telemetry + history + scene
    // hasher + cache stores that are session-scoped (program_store).
    // Use this when the session is being reused for an unrelated job
    // and the previous fingerprints no longer apply.  WP-3 close-out
    // collapsed the legacy full-reset shim into this method.  See
    // `docs/refactor-roadmap/03-render-session-boundary.md`.

    /// Per-frame reset (telemetry tracking only).  History preserved.
    void reset_frame_temporaries() {
        dirty_telemetry = RendererDirtyTelemetry{};
    }

    /// Full per-job reset (telemetry + history + scene hasher + caches).
    void reset_job() {
        // Telemetry first (frame-scoped).
        reset_frame_temporaries();
        // Then history.
        frame_history   = RendererFrameHistory{};
        layer_history   = RendererLayerHistory{};
        // Per-session scene-hasher state.
        scene_hasher    = graph::SceneHasher{};
        // Invalidate the per-session program cache so identity-keyed
        // entries don't carry over across unrelated render jobs.
        if (program_store) {
            program_store->clear();
        }
    }

    // WP-3 PR 3.4 close-out: the legacy full-reset shim was retired.
    // Callers that previously relied on the historical "erase
    // everything except arena" semantics now use `reset_job()`.
};

// SoftwareRenderSession is intentionally NOT defined here.
// Use: #include <chronon3d/backends/software/software_render_session.hpp>
// for the canonical struct (engine-generic + software-backend composition).
// This header keeps only `RenderSession` so the runtime/ layer does not
// have a struct ODR defined twice across two headers.  See PR 3.1 + 3.4
// in `docs/refactor-roadmap/03-render-session-boundary.md`.

} // namespace chronon3d
