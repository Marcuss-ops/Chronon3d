#pragma once

#include <chronon3d/core/config.hpp>
#include <chronon3d/assets/asset_registry.hpp>

// ---------------------------------------------------------------------------
// runtime/render_session.hpp
//
// TICKET-008 — Per-session rendering state, relocated from
// `include/chronon3d/core/memory/render_session.hpp` to resolve a  // drift-allow: stale-ref
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
//                                 hasher, scene program store).
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
//
// WP-3 PR 3.1 (this PR) — `SceneHasher` + `SceneProgramStore` were
// previously runtime-owned (relocated from RenderSession to RenderRuntime
// in WP-8); they are now back per-session-owned.  The TICKET-013/017
// boundary invariant that previously required forward-only declarations
// in this header is intentionally BROKEN here: per-session ownership
// requires the full type of these two state engines so `RenderSession`
// can hold them by-value (SceneHasher) or via unique_ptr (SceneProgramStore
// because it carries a std::mutex and is therefore non-movable).  See
// `docs/refactor-roadmap/03-render-session-boundary.md` for the  // drift-allow: stale-ref
// migration rationale and the architectural invariant flip.
// ===========================================================================

#include <memory>
#include <optional>

// Engine-generic field includes (acceptable from runtime/).
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
// WP-3 PR 3.1 — full type includes required by per-session-owned members.
// The previous WP-8 forward-declaring design (TICKET-013 boundary invariant)
// is intentionally lifted here because pr 3.1 requires per-session
// ownership; PIMPL would over-engineer this for a one-struct header.
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/internal/runtime/session_services.hpp>

// P1 #3 — include for the per-session TextLayoutCache member.
// TextLayoutCache has NO backend dependencies — it is a pure LRU
// cache of SharedTextRunLayout objects, safe in engine-generic code.
#include <chronon3d/text/text_run.hpp>

namespace chronon3d {

/// Engine-generic per-session rendering state.
///
/// All members are default-constructible.  FrameArena, scene_hasher, and
/// scene_program_store are stored indirectly (unique_ptr) because they
/// contain non-movable internals (std::pmr::monotonic_buffer_resource /
/// std::mutex); the unique_ptrs keep the outer struct movable.
///
/// TICKET-011a follow-up #1 — the `services` field is a non-owning
/// back-pointer bundle populated by `runtime::make_session()` so
/// session-aware contexts (currently GraphExecutor callers) can
/// read registries / caches / pools / default_assets_root through
/// the session itself instead of reaching a process-global.
///
/// WP-3 PR 3.1 (per-session ownership) — `scene_hasher` and `program_store`
/// are now by-value / unique_ptr state on the session itself.  This breaks
/// the WP-8 shared-state architecture (where every SoftwareRenderSession
/// minted from one RenderRuntime shared these two engines via the
/// SessionServices pointer bundle); the trade-off is that each session
/// is now genuinely isolated from every other session regardless of
/// shared-runtime deployment.
struct RenderSession {
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    // WP-3 PR 3.1 — per-session mutation state (renamed from
    // `scene_hasher` / `program_store` to `_state` so they don't
    // collide with the public accessor methods of the same name;
    // see the apply-minimal-fix-A migration note in
    // `docs/refactor-roadmap/03-render-session-boundary.md`).  // drift-allow: stale-ref
    //   * scene_hasher_state: by-value (struct, default-constructible, movable).
    //   * program_store_state: heap (class with std::mutex, non-movable).
    chronon3d::graph::SceneHasher scene_hasher_state{};
    std::unique_ptr<chronon3d::graph::SceneProgramStore>
        program_store_state{std::make_unique<chronon3d::graph::SceneProgramStore>()};

    // WP-3 PR 3.2 — `RendererLayerHistory` is gone; its payload lives in
    // `dirty_telemetry.previous_layers` (folded).  The struct members'
    // canonical names now mirror the renamed types: `FrameHistory` and
    // `DirtyHistory`.
    FrameHistory   frame_history;
    DirtyHistory   dirty_telemetry;
    runtime::SessionServices services;

    // P1 #3 — per-session text layout cache (replaces
    // shared_text_layout_cache() process-wide singleton).
    // TextLayoutCache uses internal PIMPL (unique_ptr<Impl>) so it is
    // lightweight (~1 pointer) and safely movable.  Default capacity
    // is 64 MiB, tunable via Config post-baseline.
    TextLayoutCache layout_cache;

    // Canonical structured error channel for the most recent frame.
    // GraphExecutor writes the existing NodeExecutionError here before
    // returning nullptr; CLI/daemon consumers only format this value and
    // do not invent a parallel error hierarchy.
    std::optional<chronon3d::graph::NodeExecutionError> last_frame_error;

    /// Per-frame reset: telemetry counters zeroed; `previous_layers`
    /// preserved (the per-layer diff source-of-truth must survive across
    /// per-frame boundaries for the dirty-rect diff to work).
    void reset_frame_temporaries() {
        dirty_telemetry.reset_telemetry_counters();
        last_frame_error.reset();
    }

    // WP-3 PR 3.1 — per-session owned; accessors return local references
    // (no throw, no reroute through SessionServices).  Production and
    // default-constructed sessions both have valid scene_hasher +
    // program_store; the throw path that the WP-8 design required
    // (services.scene_hasher was null on a default-constructed session)
    // is gone — the WP-3 PR 3.0 throw tests documented this; post 3.1
    // the tests assert the inverse: accessors NEVER throw on a freshly
    // default-constructed session.
    [[nodiscard]] chronon3d::graph::SceneHasher&       scene_hasher()       noexcept { return scene_hasher_state; }
    [[nodiscard]] const chronon3d::graph::SceneHasher& scene_hasher() const noexcept { return scene_hasher_state; }
    [[nodiscard]] chronon3d::graph::SceneProgramStore&       program_store()       noexcept { return *program_store_state; }
    [[nodiscard]] const chronon3d::graph::SceneProgramStore& program_store() const noexcept { return *program_store_state; }

    /// Arena accessor (still engine-generic; lives on the session).
    [[nodiscard]] FrameArena&       arena()       noexcept { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const noexcept { return *arena_ptr; }

    /// Full per-job reset (telemetry + history + per-session scene_hasher
    /// + per-session program_store.clear()).  WP-3 collapsed the legacy
    /// full-reset shim into this method; WP-3 PR 3.1 made the per-session
    /// ownership explicit so the reset reaches only the current session's
    /// state — never another session's, even when both were minted from
    /// the same RenderRuntime.
    // WP-3 PR 3.2 — rename-friendly; canonical types only.
    void reset_job();
};

// SoftwareRenderSession is intentionally NOT defined here.
// Use: #include <chronon3d/backends/software/software_render_session.hpp>
// for the canonical struct (engine-generic + software-backend composition).
// This header keeps only `RenderSession` so the runtime/ layer does not
// have a struct ODR defined twice across two headers.  See PR 3.1 + 3.4
// in `docs/refactor-roadmap/03-render-session-boundary.md`.  // drift-allow: stale-ref

} // namespace chronon3d
