// ===========================================================================
// src/runtime/render_session.cpp
//
// WP-8 close-out — bodies of the `RenderSession` accessor + reset_job methods
// that were previously defined inline in `<chronon3d/runtime/render_session.hpp>`.
//
// SHARED-STATE NOTE (WP-8 follow-up):
// scene_hasher + program_store are no longer per-RenderSession; they are
// engine-lifetime fields on `RenderRuntime::m_owned_scene_hasher` /
// `m_owned_program_store`.  Therefore `reset_job()` below mutates
// RUNTIME-level shared state via the SessionServices back-pointers
// (populated by `runtime::make_session()`), not per-session copies.
// In a single-runtime / single-renderer / single-session engine
// (the canonical production deployment), this matches the previous
// per-session semantics because there is only ever one instance of
// each field.  In any deployment that constructs multiple
// SoftwareRenderers from a shared RenderRuntime, or multiple
// SoftwareRenderSessions from one Runtime, scene_hasher +
// program_store state becomes genuinely shared across those
// instances; reset_job() will reach across them.  See CHANGELOG
// R5 and `docs/refactor-roadmap/03-render-session-boundary.md` PR
// 3.0 field-ownership table for the relocation.
//
// Why these live in a .cpp and not the header:
//
//   - `render_session.hpp` no longer includes
//     `<chronon3d/render_graph/core/scene_hasher.hpp>` or
//     `<chronon3d/render_graph/cache/scene_program_store.hpp>`
//     (TICKET-013 + TICKET-017 boundary invariant).
//   - The headers now hold FORWARD DECLARATIONS of `graph::SceneHasher` and
//     `graph::SceneProgramStore` only, plus declaration-only method
//     signatures that mention these types.
//   - The full types are needed here (in reset_job) to perform the
//     `*sh = graph::SceneHasher{};` assignment and the
//     `ps->clear();` call, so we include the canonical headers here.
//
// All actions proxy through `services` — the back-pointer bundle that
// `runtime::make_session()` populates from `runtime.services()`.  The
// runtime is the sole owner of `m_owned_scene_hasher` (value) and
// `m_owned_program_store` (unique_ptr); the session holds no direct
// reference to either any more.
// ===========================================================================

#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>

#include <stdexcept>

namespace chronon3d {

// ── scene_hasher / program_store accessors ───────────────────────────────
//
// Each has a non-const + const overload.  They throw if the session was
// default-constructed (i.e. its `services` table is empty — software
// renderer factory paths always populate it via `runtime::make_session()`).
//
// The throw behaviour is "safe rather than UB" — callers in
// SoftwareRenderer hold `m_runtime != nullptr` per the ctor invariant
// and route through `m_runtime->scene_hasher()` directly, sidestepping
// this method; tests that default-construct a RenderSession and then
// read a scene_hasher reference will get a loud error instead of a
// null deref.

graph::SceneHasher& RenderSession::scene_hasher() noexcept {
    if (auto* sh = services.scene_hasher) {
        return *sh;
    }
    throw std::runtime_error(
        "RenderSession::scene_hasher(): services.scene_hasher is null "
        "(session was not vended by runtime::make_session(); runtime "
        "owns this state, not the session — call m_runtime->scene_hasher() "
        "or refactor this code path to use the runtime directly).");
}

const graph::SceneHasher& RenderSession::scene_hasher() const noexcept {
    if (auto* sh = services.scene_hasher) {
        return *sh;
    }
    throw std::runtime_error(
        "RenderSession::scene_hasher() (const): services.scene_hasher is null "
        "(session was not vended by runtime::make_session())");
}

graph::SceneProgramStore& RenderSession::program_store() noexcept {
    if (auto* ps = services.program_store) {
        return *ps;
    }
    throw std::runtime_error(
        "RenderSession::program_store(): services.program_store is null "
        "(session was not vended by runtime::make_session(); runtime "
        "owns this state, not the session — call m_runtime->program_store() "
        "or refactor this code path to use the runtime directly).");
}

const graph::SceneProgramStore& RenderSession::program_store() const noexcept {
    if (auto* ps = services.program_store) {
        return *ps;
    }
    throw std::runtime_error(
        "RenderSession::program_store() (const): services.program_store is null "
        "(session was not vended by runtime::make_session())");
}

// ── reset_job ────────────────────────────────────────────────────────────
//
// WP-3 collapsed the legacy full-reset shim into `reset_job()`.
// WP-8 made the proxy explicit: scene_hasher and program_store are
// no longer members of RenderSession; the corresponding reset reaches
// through `services` to mutate the runtime-owned state.  When the
// session is default-constructed (no services), the history fields
// are still cleared locally; the runtime-owned caches are left
// untouched (no pointers to reach them).

void RenderSession::reset_job() {
    // Telemetry first (frame-scoped).
    reset_frame_temporaries();
    // Then per-session history.
    frame_history = RendererFrameHistory{};
    layer_history = RendererLayerHistory{};
    // Runtime-owned scene-hasher state — proxy via services.
    if (auto* sh = services.scene_hasher) {
        *sh = graph::SceneHasher{};
    }
    // Runtime-owned program cache — proxy via services.
    if (auto* ps = services.program_store) {
        ps->clear();
    }
}

} // namespace chronon3d
