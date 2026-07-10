#pragma once

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/result.hpp>     // Result<T,E> for create() factory

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
// `docs/refactor-roadmap/03-render-session-boundary.md` PR 3.1 + the  // drift-allow: stale-ref
// PR 3.0 doc-comment in `<chronon3d/internal/runtime/render_session.hpp>` for
// the migration rationale and the per-session ownership spec.
//
// Fase C2 — Canonical construction sequence (RenderEngine::Impl unified ctor):
//   1) m_runtime(m_config)            → populate() allocates all slots
//   2) m_renderer(m_runtime, cfg)     → renderer wires per-instance state
//   3) SoftwareBackend constructed     → inside Impl ctor body, then
//      m_runtime.attach_backend()      → runtime owns backend for engine lifetime
//   4) m_pipeline.emplace(...)         → published after backend is live
//
// @deprecated standalone paths (migrate to RenderEngine constructor):
//   * SoftwareRenderer(Config) — creates its own runtime internally
//   * RenderRuntime::attach_backend() — use RenderEngine::Impl unified ctor
// ----------------------------------------------------------------------

#include <cassert>
#include <chrono>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
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

#include <filesystem>
#include <memory>
#include <optional>
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

// ═══════════════════════════════════════════════════════════════════════════
// Fase C2 — RuntimeBuildError + RuntimeConfig + create() factory
// ═══════════════════════════════════════════════════════════════════════════

/// Structured error returned by RenderRuntime::create() when construction fails.
struct RuntimeBuildError {
    enum class Code {
        InternalError,      ///< Unspecified failure during populate().
        AssetMountFailed,   ///< assets_root path could not be mounted.
    };

    Code        code{Code::InternalError};
    std::string message;
};

/// Configuration bundle for the unified RenderRuntime::create() factory.
/// Wraps engine Config with an optional assets_root path (seeds the
/// per-runtime resolver, replacing the process-wide global fallback).
struct RuntimeConfig {
    chronon3d::Config                           config;
    std::optional<std::filesystem::path>        assets_root;
};

// ═══════════════════════════════════════════════════════════════════════════
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
    chronon3d::cache::CacheDiagnostics*          diagnostics{nullptr};
    chronon3d::cache::PersistentFramebufferStore* framebuffer_store{nullptr};
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

    // ── Fase C2 — Unified factory (canonical construction path) ──────────
    /// Static factory: constructs a fully-populated RenderRuntime from
    /// a RuntimeConfig bundle.  Returns a heap-allocated runtime (unique_ptr)
    /// on success, or RuntimeBuildError on failure.  RenderRuntime is not
    /// movable (contains non-movable mutex-guarded types like AssetRegistry,
    /// AssetResolver, ImageCache), so the factory returns ownership via
    /// unique_ptr — the canonical pattern for non-movable types.
    /// Backend attachment is handled by the higher-level RenderEngine layer.
    [[nodiscard]] static Result<std::unique_ptr<RenderRuntime>, RuntimeBuildError>
    create(RuntimeConfig cfg);

    // Non-copyable, non-movable (contains mutex-guarded types).
    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;
    RenderRuntime(RenderRuntime&&) = delete;
    RenderRuntime& operator=(RenderRuntime&&) = delete;

    /// Initialise the long-lived infrastructure from the engine Config.
    /// Idempotent: calling populate() on a populated runtime is a no-op.
    /// After populate() the service-locator bundle is populated, the
    /// pipeline catalogs are wired, and the asset registry is initialised.
    /// The backend is NOT allocated here — see attach_backend().
    void populate();

    /// @deprecated Fase C2 — use RenderRuntime::create(RuntimeConfig)
    /// for new code.  Backend attachment is now the responsibility of
    /// higher-level orchestration (RenderEngine::Impl, runtime_adapter).
    /// Internal bridge sites (runtime_adapter.cpp, test utils) may
    /// suppress -Wdeprecated-declarations when calling this method.
    [[deprecated("Use RenderRuntime::create(RuntimeConfig) instead; internal bridges may suppress this warning")]]
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

    // ── Image cache (Fase B B1 — per-runtime, no longer process-wide) ─
    [[nodiscard]] chronon3d::ImageCache&       image_cache()       noexcept { return m_image_cache; }
    [[nodiscard]] const chronon3d::ImageCache& image_cache() const noexcept { return m_image_cache; }

    // ── Pipeline catalogs ────────────────────────────────────────────
    [[nodiscard]] chronon3d::graph::PipelineCatalogs& catalogs() noexcept { return m_catalogs; }
    [[nodiscard]] const chronon3d::graph::PipelineCatalogs& catalogs() const noexcept { return m_catalogs; }

    // ── Service-locator bundle (Fase 5 — const-only read access) ──────
    // The RenderServices bundle is the canonical wiring point for
    // internal bridges (runtime_adapter, test_utils).  External
    // consumers should use the typed direct accessors below
    // (node_cache(), executor(), framebuffer_pool(), etc.).
    // The mutable overload was REMOVED in Fase 5 (TICKET-P1-09)
    // — services are configured once in populate() and must not
    // be mutated externally.
    [[nodiscard]] const RenderServices& services() const noexcept { return m_services; }

    // ── Direct accessors used by SoftwareRenderer (forwarders for
    //    caller convenience; primary access is via services()) ───────
    [[nodiscard]] chronon3d::AssetRegistry&               assets()         noexcept { return m_assets; }
    // ── WP-8 PR 8.0 typed asset resolver (sibling of m_assets) ───────
    [[nodiscard]] chronon3d::assets::AssetResolver&       resolver()       noexcept { return m_resolver; }
    [[nodiscard]] const chronon3d::assets::AssetResolver& resolver() const noexcept { return m_resolver; }
    [[nodiscard]] chronon3d::cache::CacheDiagnostics&       diagnostics()       noexcept { return m_diagnostics; }
    [[nodiscard]] const chronon3d::cache::CacheDiagnostics& diagnostics() const noexcept { return m_diagnostics; }
    [[nodiscard]] chronon3d::cache::PersistentFramebufferStore&       framebuffer_store()       noexcept { return m_framebuffer_store; }
    [[nodiscard]] const chronon3d::cache::PersistentFramebufferStore& framebuffer_store() const noexcept { return m_framebuffer_store; }
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
    // `SoftwareRenderSession`).  See `docs/refactor-roadmap/03-render-session-boundary.md`.  // drift-allow: stale-ref

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

    // Fase B B1 — per-runtime image cache (replaces process-wide singleton)
    chronon3d::ImageCache                           m_image_cache;
    // diag accessor: per-runtime CacheDiagnostics instance (value member; construction happens
    // at object-init time so even pre-populate() callers can use diagnostics() directly.
    // The friend declaration in cache_diagnostics.hpp gives RenderRuntime access to the
    // private default ctor.)
    chronon3d::cache::CacheDiagnostics                  m_diagnostics{};
    // framebuffer_store accessor: per-runtime PersistentFramebufferStore (value member); note that
    // the static config helpers set_store_config / enabled_for_current_run remain process-wide
    // bootstrap state and continue to use the singleton via the now-deprecated instance().
    chronon3d::cache::PersistentFramebufferStore                  m_framebuffer_store{};

    std::unique_ptr<chronon3d::graph::RenderBackend>   m_backend;
    bool                                              m_populated{false};
};

// Fase B2 (DONE) — process_wide_assets_root() / process_wide_resolver() REMOVED.
// Production code must pass AssetResolver& through the call chain
// (RenderRuntime::resolver(), RenderSession, or dependency injection).
// Deep code without a runtime in scope should receive the resolver via
// parameter rather than reading a process-wide global.

} // namespace chronon3d::runtime
