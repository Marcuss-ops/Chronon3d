#pragma once

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/config.hpp>

// ----------------------------------------------------------------------
// runtime/render_runtime.hpp
//
// TICKET-011 — RenderRuntime is now the SOLE engine-lifetime owner of
// long-lived render infrastructure.  `SoftwareRenderer` no longer
// holds any of these fields; it borrows everything via `RenderRuntime&`.
//
// Owned slots:
//   - Config                                       (engine config copy)
//   - AssetRegistry                                (mounts paths)
//   - cache::NodeCache                             (per-job node cache)
//   - cache::FramebufferPool (shared_ptr)          (transitively held by
//                                                    the SoftwareBackend)
//   - graph::CompiledGraphCache                    (graph reuse across
//                                                    frames)
//   - graph::PipelineCatalogs                      (graph_nodes + effects
//                                                    + extensions +
//                                                    precomp_builder)
//   - graph::ExecutionScheduler                     (tbb::task_arena owner)
//   - graph::GraphExecutor                          (stateless executor)
//   - renderer::SoftwareRegistry                    (shape processor reg.)
//   - graph::GraphNodeCatalog                       (graph node registry)
//   - effects::EffectCatalog                        (effect registry)
//   - unique_ptr<RenderBackend>                     (attached externally
//                                                    via attach_backend()
//                                                    because SoftwareBackend
//                                                    ctor needs the
//                                                    renderer's
//                                                    RenderCounters & +
//                                                    RenderSettings & —
//                                                    per-instance state
//                                                    that lives on
//                                                    SoftwareRenderer)
//
// WP-3 PR 3.1 — `SceneHasher` and `SceneProgramStore` are no longer
// runtime-owned.  They were relocated from RenderSession to RenderRuntime
// in WP-8 (see prior revisions of this header) and are now BACK per-
// session-owned.  Each `RenderSession` carries its own `scene_hasher`
// (by-value) and `program_store` (unique_ptr).  The runtime therefore
// no longer populates them in `populate()`, no longer assigns them into
// the `RenderServices` bundle, and no longer wires them into the
// `SessionServices` table via `make_session()`.  See
// `docs/refactor-roadmap/03-render-session-boundary.md` PR 3.1 + the
// PR 3.0 doc-comment in `<chronon3d/internal/runtime/render_session.hpp>` for
// the migration rationale and the per-session ownership spec.
//
// Construction sequence (RenderEngine::Impl drives this):
//   1) m_runtime(m_config)         → populate() allocates all slots
//   2) m_renderer(m_runtime, cfg)  → renderer wires its per-instance
//                                    state (counters, settings, image
//                                    backend, video decoder, session)
//   3) m_runtime.attach_backend(   → render_engine constructs
//        make_unique<SoftwareBackend>  SoftwareBackend with
//        (m_renderer->counters(),     (renderer-owned counters + settings
//         m_renderer->settings(),     + runtime-owned pool) and
//         m_runtime.framebuffer_pool_ attaches it to the runtime
//         shared()));
// ----------------------------------------------------------------------

#include <cassert>
#include <chrono>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
// Fase 4 — software-registry and software-render-session includes removed.
// SoftwareRegistry ownership moved to SoftwareRenderer; make_session /
// session_services moved to backends/software/runtime_adapter.hpp.

#include <memory>
#include <string>

namespace chronon3d {
    struct Config;
    struct RenderSettings;
    class DebugConfig;
    class RenderBackend;
}

namespace chronon3d::cache {
    class NodeCache;
    class FramebufferPool;
}

namespace chronon3d::runtime {

/// Engine-generic service-locator bundle owned by RenderRuntime.
///
/// HUD: flat pointer bundle so that sessions / per-call contexts
/// can read registries and caches via `runtime.services()` rather than
/// reaching into SoftwareRenderer's private members.  Pointers are
/// non-owning; lifetime is the runtime's responsibility.
///
/// NOTE: distinct from `chronon3d::graph::RenderServices` (per-
/// `RenderGraphContext` service locator; per-frame rather than per-
/// engine).
struct RenderServices {
    chronon3d::AssetRegistry*                asset_registry{nullptr};
    /// WP-8 PR 8.0 — typed engine-local asset path resolver.  Sibling
    /// of asset_registry; PR 8.1 routes deep asset consumers here.
    chronon3d::assets::AssetResolver*        asset_resolver{nullptr};
    chronon3d::cache::NodeCache*              node_cache{nullptr};
    chronon3d::cache::FramebufferPool*        framebuffer_pool{nullptr};
    chronon3d::graph::CompiledGraphCache*     graph_cache{nullptr};
    chronon3d::ExecutionScheduler*            scheduler{nullptr};
    chronon3d::graph::GraphExecutor*          executor{nullptr};
    chronon3d::graph::GraphNodeCatalog*       graph_node_registry{nullptr};
    chronon3d::effects::EffectCatalog*        effect_catalog{nullptr};
    // WP-3 PR 3.1 — `scene_hasher` and `program_store` pointer fields
    // were REMOVED here.  Both state engines are now per-session owned
    // (see `RenderSession::scene_hasher` / `RenderSession::program_store`).
    // Callers that previously read `runtime.services().scene_hasher` /
    // `runtime.services().program_store` must now read the session
    // directly: `session.scene_hasher()` / `session.program_store()`.
};

/// RenderRuntime — engine-lifetime container.
class RenderRuntime {
public:
    RenderRuntime() = default;
    explicit RenderRuntime(chronon3d::Config config);
    ~RenderRuntime();

    // Non-copyable, movable.
    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;
    RenderRuntime(RenderRuntime&&) noexcept = default;
    RenderRuntime& operator=(RenderRuntime&&) noexcept = default;

    /// Initialise the long-lived infrastructure from the engine Config.
    /// Idempotent: calling populate() on a populated runtime is a no-op.
    /// After populate() the service-locator bundle is populated, the
    /// pipeline catalogs are wired, and the asset registry is initialised.
    /// The backend is NOT allocated here — see attach_backend().
    void populate();

    /// Attach the engine's `RenderBackend` implementation.  Called by
    /// RenderEngine::Impl after SoftwareRenderer is constructed (the
    /// backend ctor needs the renderer's per-instance counters +
    /// settings, which live on the renderer, not on the runtime).
    /// After attach_backend() the runtime is the sole owner of the
    /// backend for the rest of the engine lifetime.
    void attach_backend(std::unique_ptr<chronon3d::graph::RenderBackend> backend);

    // ── Configuration ────────────────────────────────────────────────
    [[nodiscard]] const chronon3d::Config& config() const noexcept { return m_config; }

    // ── Backend access (populated after attach_backend()) ────────────
    // WP-0 PR 0.1 — `noexcept` was REMOVED from the declaration: the
    // body throws std::runtime_error on unattached backend, so a
    // `noexcept` declaration would terminate the process instead of
    // surfacing the error.  See `tools/check_architecture_boundaries.sh`
    // check [11/12] for the regression guard.
    [[nodiscard]] chronon3d::graph::RenderBackend& backend();
    [[nodiscard]] const chronon3d::graph::RenderBackend& backend() const;

    // ── Backend slot predicates ──────────────────────────────────────
    [[nodiscard]] bool backend_attached() const noexcept { return static_cast<bool>(m_backend); }

    // ── Pipeline catalogs ────────────────────────────────────────────
    [[nodiscard]] chronon3d::graph::PipelineCatalogs& catalogs() noexcept { return m_catalogs; }
    [[nodiscard]] const chronon3d::graph::PipelineCatalogs& catalogs() const noexcept { return m_catalogs; }

    // ── Service-locator bundle ───────────────────────────────────────
    [[nodiscard]] RenderServices& services() noexcept { return m_services; }
    [[nodiscard]] const RenderServices& services() const noexcept { return m_services; }

    // ── Direct accessors used by SoftwareRenderer (forwarders for
    //    caller convenience; primary access is via services()) ───────
    [[nodiscard]] chronon3d::AssetRegistry&               assets()         noexcept { return m_assets; }
    // ── WP-8 PR 8.0 typed asset resolver (sibling of m_assets) ───────
    [[nodiscard]] chronon3d::assets::AssetResolver&       resolver()       noexcept { return m_resolver; }
    [[nodiscard]] const chronon3d::assets::AssetResolver& resolver() const noexcept { return m_resolver; }
    [[nodiscard]] chronon3d::cache::NodeCache&             node_cache()     noexcept { return m_owned_node_cache; }
    [[nodiscard]] chronon3d::graph::CompiledGraphCache&    graph_cache()    noexcept { return m_owned_graph_cache; }
    [[nodiscard]] std::shared_ptr<chronon3d::cache::FramebufferPool> framebuffer_pool_shared() noexcept { return m_owned_framebuffer_pool; }
    [[nodiscard]] chronon3d::cache::FramebufferPool&       framebuffer_pool() noexcept {
        return *m_owned_framebuffer_pool;
    }
    [[nodiscard]] chronon3d::graph::GraphExecutor&         executor()       noexcept { return *m_owned_executor; }

    [[nodiscard]] chronon3d::graph::GraphNodeCatalog&      graph_node_registry() noexcept { return *m_owned_graph_node_registry; }
    [[nodiscard]] chronon3d::effects::EffectCatalog&       effect_catalog() noexcept { return *m_owned_effect_catalog; }
    [[nodiscard]] chronon3d::ExecutionScheduler&           scheduler()      noexcept { return *m_scheduler; }

    // WP-3 PR 3.1 — `scene_hasher()` + `program_store()` accessors were
    // REMOVED here.  Both state engines are now per-session owned; reach
    // them via `session.scene_hasher()` / `session.program_store()`
    // (or `session.common.scene_hasher()` / `program_store()` from a
    // `SoftwareRenderSession`).  See `docs/refactor-roadmap/03-render-session-boundary.md`.

private:
    chronon3d::Config                                   m_config;
    chronon3d::graph::PipelineCatalogs                  m_catalogs;
    chronon3d::AssetRegistry                            m_assets;
    /// WP-8 PR 8.0 — typed asset resolver, sibling of m_assets; value
    /// member so lifetime is the runtime's, deterministic per engine.
    chronon3d::assets::AssetResolver                    m_resolver;
    RenderServices                                      m_services{{}};

    chronon3d::cache::NodeCache                         m_owned_node_cache{};
    std::shared_ptr<chronon3d::cache::FramebufferPool> m_owned_framebuffer_pool;
    chronon3d::graph::CompiledGraphCache                m_owned_graph_cache{};
    std::unique_ptr<chronon3d::graph::GraphExecutor>         m_owned_executor;
    std::unique_ptr<chronon3d::graph::GraphNodeCatalog>       m_owned_graph_node_registry;
    std::unique_ptr<chronon3d::effects::EffectCatalog>        m_owned_effect_catalog;
    std::unique_ptr<chronon3d::ExecutionScheduler>            m_scheduler;
    // WP-3 PR 3.1 — `m_owned_scene_hasher` and `m_owned_program_store`
    // were REMOVED.  Both are now per-session owned (see
    // `RenderSession::scene_hasher` / `RenderSession::program_store`).
    // The runtime no longer reaches into them; if a future feature
    // genuinely needs cross-session reach, the right place is via
    // an `ExecutionScope` abstraction (WP-6 scope) or a service helper
    // — not a free-floating runtime-owned instance.

    std::unique_ptr<chronon3d::graph::RenderBackend>   m_backend;
    bool                                              m_populated{false};
};

// ── Process-wide assets root (Fase B — P1 #2: DEPRECATED) ────────────
//
// @deprecated Fase B — process-wide global state.  Migrate to
// RenderRuntime-owned resolver (m_resolver) reachable via
// `runtime.resolver()`.  Deep code without a RenderRuntime in scope
// should receive the resolver through dependency injection
// (RenderRequest → RenderSession → RenderGraphContext → AssetResolver&)
// rather than reading a process-wide global.
//
// Current callers (~24 references across tests, CLI, content modules):
// set_process_wide_assets_root: render_job_setup.cpp, test_main.cpp,
//   render_engine.cpp, content tests
// process_wide_assets_root: render_engine.cpp (assets_root())
// process_wide_resolver: text_run_driver.cpp, content modules, tests
//
// Migration blocked by the breadth of call sites.  Tracked for
// Phase C (post-feature-freeze).
void set_process_wide_assets_root(std::string root);
/// @deprecated Fase B — process-wide global; see deprecation banner above.
/// Returns by value (not by reference) so callers cannot hold a pointer
/// past a concurrent `set_process_wide_assets_root` which would move-assign
/// the underlying string.  Empty if nothing has been configured.
[[nodiscard]] std::string process_wide_assets_root();

/// @deprecated Fase B — process-wide lazy-static singleton; see
/// deprecation banner above.  Migrate to `runtime.resolver()`.
/// Lazy-static; first-mount semantics against `process_wide_assets_root()`;
/// thread-safety via resolver's internal `shared_mutex`.  Lifetime is the
/// process.  WP-8 PR 8.1 migration target.
[[nodiscard]] const chronon3d::assets::AssetResolver&
process_wide_resolver();

} // namespace chronon3d::runtime
