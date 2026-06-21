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
//   - std::string default_assets_root               (engine-local; replaces
//                                                    the migration bridge
//                                                    to detail::g_default_
//                                                    assets_root)
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
// PR 3.0 doc-comment in `<chronon3d/runtime/render_session.hpp>` for
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
#include <chronon3d/runtime/render_session.hpp>
// WP-8 PR 8.0 stop-gap — `SoftwareRenderSession` is needed for `make_session`
// and `session_services` (render-side software types).  WP-8 PR 8.4
// moves those APIs to a software-internal header; revert this include
// at that point.
#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
// WP-3 PR 3.4 — canonical location for the renamed SoftwareRenderSession.
#include <chronon3d/backends/software/software_render_session.hpp>
// WP-3 PR 3.1 — scene_program_store.hpp and scene_hasher.hpp are no
// longer needed by render_runtime.hpp.  The two state engines are now
// per-session owned (see render_session.hpp), so the runtime doesn't
// reach into them directly and doesn't need their full types.

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
    chronon3d::renderer::SoftwareRegistry*    software_registry{nullptr};
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
    [[nodiscard]] chronon3d::graph::RenderBackend& backend() noexcept;
    [[nodiscard]] const chronon3d::graph::RenderBackend& backend() const noexcept;

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
    [[nodiscard]] chronon3d::renderer::SoftwareRegistry&   software_registry() noexcept { return *m_owned_software_registry; }
    [[nodiscard]] chronon3d::graph::GraphNodeCatalog&      graph_node_registry() noexcept { return *m_owned_graph_node_registry; }
    [[nodiscard]] chronon3d::effects::EffectCatalog&       effect_catalog() noexcept { return *m_owned_effect_catalog; }
    [[nodiscard]] chronon3d::ExecutionScheduler&           scheduler()      noexcept { return *m_scheduler; }

    // WP-3 PR 3.1 — `scene_hasher()` + `program_store()` accessors were
    // REMOVED here.  Both state engines are now per-session owned; reach
    // them via `session.scene_hasher()` / `session.program_store()`
    // (or `session.common.scene_hasher()` / `program_store()` from a
    // `SoftwareRenderSession`).  See `docs/refactor-roadmap/03-render-session-boundary.md`.

    // ── Default assets root ──────────────────────────────────────────
    [[nodiscard]] const std::string& default_assets_root() const noexcept { return m_default_assets_root; }

    void set_default_assets_root(std::string root);

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
    std::unique_ptr<chronon3d::renderer::SoftwareRegistry>   m_owned_software_registry;
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
    std::string                                       m_default_assets_root;
    bool                                              m_populated{false};
};

// ── Process-wide "active runtime" pointer ───────────────────────────
//
// Deep rendering code that has no direct access to a RenderRuntime
// (e.g. font_engine, text_rasterizer) reads
// `default_assets_root_for_deep_code()` to find the engine's default
// assets root.  This is the migration bridge that replaces
// `detail::g_default_assets_root`; the bridge is named live in
// render_runtime.cpp so its definition lives next to the writes it
// serves.

void set_active_runtime(RenderRuntime* runtime);
[[nodiscard]] RenderRuntime* active_runtime();
/// Returns the engine's default assets root for deep rendering code
/// that has no direct RenderRuntime in its call stack (font_engine,
/// text_rasterizer, preflight, etc.).  Resolution order:
///
///   1. Active RenderRuntime's `default_assets_root()` if a runtime
///      is `active_runtime()`-reachable.
///   2. Typed `process_wide_assets_root()` fallback for contexts that
///      don't construct a RenderRuntime.
///
/// Returns an empty string when neither branch is configured
/// (callers via `typed_resolver_for_deep_code` should treat that as
/// an unmounted resolver — there's no root to mount relative paths
/// against).  Returned BY VALUE so both branches copy into the
/// return slot before this function returns — branch 1's reference
/// always outlives the call (runtime is active) and branch 2 is
/// already a `std::string` value.
[[nodiscard]] std::string default_assets_root_for_deep_code();

/// Vends a fresh per-render-job `SoftwareRenderSession` whose
/// `common.services` field is populated with the runtime's catalogue
/// back-pointers.  Lifetime invariant: `runtime` must outlive every
/// session it vends.
[[nodiscard]] chronon3d::SoftwareRenderSession make_session(RenderRuntime& runtime);

/// Read the back-pointers currently bound to a session.  Returns an
/// empty record if the session was not produced by `make_session()`
/// (e.g. default-constructed by a test fixture).
[[nodiscard]] const SessionServices& session_services(const chronon3d::SoftwareRenderSession& session);

// ── Process-wide assets root (TICKET-011a follow-up #2) ────────────
//
// Set by CLI/test init paths that don't construct a RenderRuntime.  Read
// by `default_assets_root_for_deep_code()` as a fallback when no
// RenderRuntime is active.  RenderRuntime::set_default_assets_root also
// writes here so engine init propagates to deep code across the same
// process without a separate signal.
void set_process_wide_assets_root(std::string root);
/// Return the process-wide assets-root fallback by value.  Empty if
/// nothing has been configured.  Returning by value (not by reference)
/// keeps the per-function-call mutex guard honest: a returned
/// reference would dangle if another thread concurrently moved the
/// underlying module-level string via `set_process_wide_assets_root`.
[[nodiscard]] std::string process_wide_assets_root();

/// WP-8 PR 8.2 — the legacy single-argument free function
/// `runtime::resolve_asset_path(relative_path)` has been REMOVED.
/// Every deep call site has been migrated to the typed resolver path
/// (see `typed_resolver_for_deep_code()` doc-comment below for the
/// canonical call shape).  New code MUST use the typed resolver —
/// there is no backwards-compatibility wrapper and no
/// "just-construct-the-string" fallback path.

/// WP-8 PR 8.1 — typed engine-local asset resolver for callers that
/// have no `RenderRuntime` in their call stack (font_engine,
/// text_rasterizer, preflight analysis, text_run BL/FT caches, etc.).
///
/// Resolution order:
///
///   1. The active runtime's `AssetResolver` if `active_runtime()`
///      returns non-null.  This is the per-engine, deterministic
///      resolver the runtime owns — same resolver instance used by the
///      runtime itself and surfaced through `runtime.services()`.
///
///   2. A lazy-initialised process-wide singleton resolver mounted
///      against `process_wide_assets_root()`.  Cached in function-local
///      storage across calls; mounted once on first use.
///
///   3. A default-constructed (unmounted) resolver if neither path is
///      configured.  Callers detect this via the resolver's optional
///      return value.
///
/// Threading: AssetResolver::mount() acquires its own internal
/// `shared_mutex`/`std::mutex`, so concurrent first-callers that all
/// pick up the same process-wide root are serialized inside the
/// resolver and the resulting state is idempotent.  No external
/// `std::once_flag` is required.
///
/// Replaces the legacy `runtime::resolve_asset_path(relative)` usage at
/// every deep-call-site migration.  Old callers should be rewritten to
/// the 2-line pattern documented in the call-site migration notes:
///   ```
///   const auto& resolver =
///       chronon3d::runtime::typed_resolver_for_deep_code();
///   auto resolved_opt = resolver.resolve_lexical(rel_path);
///   const std::string resolved_path =
///       resolved_opt ? resolved_opt->string()
///                    : (rel_path.empty() ? std::string{}
///                                        : std::string{rel_path});
///   ```
[[nodiscard]] const chronon3d::assets::AssetResolver&
typed_resolver_for_deep_code();

namespace detail {

/// WP-8 PR 8.1 — test-only reset hook.  Unmounts the process-wide
/// fallback singleton resolver inside `typed_resolver_for_deep_code()`
/// so each test fixture starts with a clean slate.  Production code
/// MUST NOT call this — the static cache is first-mount-only by
/// design (see `typed_resolver_for_deep_code()` doc-comment).  Tests
/// that exercise the process-wide branch should bracket their
/// assertions with this reset + a fresh
/// `set_process_wide_assets_root(...)` + helper call.
void reset_typed_resolver_for_deep_code_for_testing();

} // namespace detail

} // namespace chronon3d::runtime
