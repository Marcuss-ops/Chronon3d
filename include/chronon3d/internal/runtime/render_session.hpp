#pragma once

#include <chronon3d/core/config.hpp>
#include <chronon3d/assets/asset_registry.hpp>

// ---------------------------------------------------------------------------
// runtime/render_session.hpp
//
// TICKET-008 — Per-session rendering state, relocated to
// `include/chronon3d/internal/runtime/render_session.hpp` (its current
// location) to resolve a
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
// `docs/refactor-roadmap/03-render-session-boundary.md` for the  // drift-class: historical (WP-3 design doc retired; rationale in this header block)
// migration rationale and the architectural invariant flip.
// ===========================================================================

#include <memory>
#include <mutex>

// Engine-generic field includes (acceptable from runtime/).
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/math/renderer_state.hpp>
// WP-3 PR 3.1 — full type includes required by per-session-owned members.
// The previous WP-8 forward-declaring design (TICKET-013 boundary invariant)
// is intentionally lifted here because pr 3.1 requires per-session
// ownership; PIMPL would over-engineer this for a one-struct header.
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/internal/runtime/session_services.hpp>
#include <chronon3d/internal/runtime/history_state.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>

// P1 #3 — include for the per-session TextLayoutCache member.
// TextLayoutCache has NO backend dependencies — it is a pure LRU
// cache of SharedTextRunLayout objects, safe in engine-generic code.
#include <chronon3d/text/text_run.hpp>

namespace chronon3d {

class Composition;
struct CompiledComposition;
namespace graph {
struct NodeExecutionError;
class ExecutionWorkspaceRing;
}

/// Thread-safe storage for the existing graph::NodeExecutionError channel.
/// This is not a second error framework: it only preserves the first error
/// emitted by parallel tile/precomp execution until the CLI consumes it.
class RenderErrorSlot final {
public:
    void clear() {
        std::lock_guard lock(m_mutex);
        m_error.reset();
    }

    void publish_first(const graph::NodeExecutionError& error) {
        std::lock_guard lock(m_mutex);
        if (!m_error) {
            m_error = std::make_shared<const graph::NodeExecutionError>(error);
        }
    }

    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError> load() const {
        std::lock_guard lock(m_mutex);
        return m_error;
    }

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const graph::NodeExecutionError> m_error;
};

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

    // Generic graph execution storage.  It is shared by all node domains;
    // nested executions lease another slot instead of resetting a parent.
    std::unique_ptr<chronon3d::graph::ExecutionWorkspaceRing> execution_workspaces{
        std::make_unique<chronon3d::graph::ExecutionWorkspaceRing>()};

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

    // Per-session node/sample memory accounting. The implementation is
    // internal; ownership follows the session so temporal samples cannot
    // publish counters into the main render session.
    std::unique_ptr<graph::NodeMemoryTracker> memory_tracker{
        std::make_unique<graph::NodeMemoryTracker>()};

    // Heap-owned because RenderSession must remain movable while the slot
    // itself contains a mutex. Reset happens once at the top-level renderer
    // boundary; nested executors only publish_first().
    std::unique_ptr<RenderErrorSlot> frame_error_slot{
        std::make_unique<RenderErrorSlot>()};

    const CompiledComposition* prepared_composition{nullptr};
    std::string authoring_composition_name{};
    // The authoring callback may capture different scene state while keeping
    // the same display name. Name-only reuse would render the first compiled
    // composition for every later object (notably breaking depth-dependent
    // tests and generated clips).
    std::uint64_t authoring_composition_identity{0};
    std::shared_ptr<const CompiledComposition> authoring_compiled_composition{nullptr};

    void clear_last_frame_error() {
        frame_error_slot->clear();
    }

    void publish_last_frame_error(const graph::NodeExecutionError& error) {
        frame_error_slot->publish_first(error);
    }

    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError>
    last_frame_error() const {
        return frame_error_slot->load();
    }

    /// Per-frame reset: telemetry counters zeroed; `previous_layers`
    /// preserved (the per-layer diff source-of-truth must survive across
    /// per-frame boundaries for the dirty-rect diff to work).
    void reset_frame_temporaries() {
        dirty_telemetry.reset_telemetry_counters();
        clear_last_frame_error();
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

    /// Reset only frame-scoped values and diagnostics. Preserves temporal
    /// history, compiled topology and runtime-owned caches.
    void reset_frame_values() {
        reset_frame_temporaries();
        program_store_state->clear();
        layout_cache.clear();
        // Memory reports are session-scoped and must not leak into the next
        // frame-value lifetime boundary.
        memory_tracker->reset();
    }

    /// Formal non-owning temporal-history domain facade. The session remains
    /// the sole owner of the underlying FrameHistory and DirtyHistory.
    /// Ephemeral non-owning view; do not retain beyond this session's lifetime.
    [[nodiscard]] runtime::FrameHistoryState history_state() noexcept {
        return runtime::FrameHistoryState{frame_history, dirty_telemetry};
    }

    /// Reset only temporal/session history and framebuffer-related state.
    /// Compiled topology and runtime-owned frame-value caches are preserved.
    void reset_temporal_history();

    /// Reset session-local scene evaluation state without touching caches.
    void reset_scene_state() noexcept;

    /// Full per-job reset (values + temporal history + per-session state).
    /// Runtime-owned compiled topology and node-value caches are intentionally
    /// not touched here; their independent reset APIs live on RenderRuntime.
    void reset_job();
};

// SoftwareRenderSession is intentionally NOT defined here.
// Use: #include <chronon3d/backends/software/software_render_session.hpp>
// for the canonical struct (engine-generic + software-backend composition).
// This header keeps only `RenderSession` so the runtime/ layer does not
// have a struct ODR defined twice across two headers.  See PR 3.1 + 3.4
// in `docs/refactor-roadmap/03-render-session-boundary.md`.  // drift-allow: stale-ref

} // namespace chronon3d
