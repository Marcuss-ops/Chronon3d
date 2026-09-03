#pragma once

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/media_session_pool.hpp>
#include <chronon3d/runtime/overlay_template.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/result.hpp>     // Result<T,E> for create() factory
#include <chronon3d/effects/curves.hpp>

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
// runtime-owned.  Each `RenderSession` carries its own `scene_hasher`
// (by-value) and `program_store` (unique_ptr).  See the WP-3 PR 3.1 note and
// the PR 3.0 doc-comment in `<chronon3d/internal/runtime/render_session.hpp>` for
// the migration rationale and the per-session ownership spec.
//
// Fase C2 — Canonical construction sequence (RenderEngine::Impl unified ctor):
//   1) RenderRuntime::create(RuntimeConfig) → populate() allocates all slots
//   2) m_renderer(m_runtime, cfg)           → renderer wires per-instance state
//   3) SoftwareBackend constructed          → inside Impl ctor body, then
//      m_runtime.attach_backend()           → runtime owns backend for engine lifetime
//   4) m_pipeline.emplace(...)              → published after backend is live
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
#include <mutex>
#include <string>

namespace chronon3d {
    struct Config;
    struct RenderSettings;
    class DebugConfig;
    class FontEngine;       // WP-9 PR 9.0 — runtime FontEngine forward decl
    class RenderBackend;
    // P1-14 — forward decl for SoftwareRenderer (target of the
    // attach_software_backend friend bridges declared inside RenderRuntime).
    class SoftwareRenderer;
}

// P1-14 — friend function forward declarations for `attach_software_backend`
// in the production bridge (runtime_adapter) and the test bridge (test_utils).
// Defined in their respective TUs; the friend declarations inside
// RenderRuntime grant them access to the now-private attach_backend().
namespace chronon3d::backends::software {
    void attach_software_backend(::chronon3d::SoftwareRenderer*);
    void attach_software_backend(
        ::chronon3d::SoftwareRenderer*,
        ::chronon3d::graph::BackendPreference);
}
namespace chronon3d::test {
    void attach_software_backend(::chronon3d::SoftwareRenderer*);
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

class RenderRuntime;

// P1-15 (DONE) — the `RenderServices` pointer bundle (struct +
// `services()` accessor + `m_services` member) has been REMOVED
// wholesale.  The typed direct accessors below are the canonical
// surface.  (graph::RenderServices in render_graph_context.hpp is
// a distinct per-frame bundle — unaffected.)

/// RenderRuntime — engine-lifetime container.
class RenderRuntime {
public:
    // The default and direct Config constructors are intentionally not public:
    // RenderRuntime::create(RuntimeConfig) is the sole construction boundary.
    RenderRuntime() = delete;
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

    // P1-14 — `populate()` and `attach_backend()` moved to PRIVATE (see
    // below). The canonical public entry is
    // `RenderRuntime::create(RuntimeConfig)`; the factory's private
    // constructor calls `populate()`, while `attach_backend()` is called
    // by the internal bridges (runtime_adapter + test_utils) listed as
    // `friend` declarations in the private section.

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
    [[nodiscard]] chronon3d::CurveCache& curve_cache() noexcept { return m_curve_cache; }
    [[nodiscard]] const chronon3d::CurveCache& curve_cache() const noexcept { return m_curve_cache; }

    // ── Pipeline catalogs ────────────────────────────────────────────
    [[nodiscard]] chronon3d::graph::PipelineCatalogs& catalogs() noexcept { return m_catalogs; }
    [[nodiscard]] const chronon3d::graph::PipelineCatalogs& catalogs() const noexcept { return m_catalogs; }

    // ── Typed direct accessors (P1-15 canonical surface) ───────────
    // The legacy `Runtime::services()` accessor + `RenderServices`
    // service-locator bundle was REMOVED in P1-15.  External
    // consumers use these typed accessors (each returns the canonical
    // reference / pointer / shared_ptr for that subsystem).  Internal
    // bridges (runtime_adapter, test_utils) do the same — there is no
    // longer a service-locator alternative.
    [[nodiscard]] chronon3d::AssetRegistry&               assets()         noexcept { return m_assets; }
    // ── WP-8 PR 8.0 typed asset resolver (sibling of m_assets) ───────
    [[nodiscard]] chronon3d::assets::AssetResolver&       resolver()       noexcept { return m_resolver; }
    [[nodiscard]] const chronon3d::assets::AssetResolver& resolver() const noexcept { return m_resolver; }
    [[nodiscard]] chronon3d::assets::MeshPreparationCache& mesh_cache() noexcept { return m_mesh_cache; }
    [[nodiscard]] const chronon3d::assets::MeshPreparationCache& mesh_cache() const noexcept { return m_mesh_cache; }

    /// Publish the immutable resource snapshot produced by the preparation
    /// barrier. RenderGraph borrows this snapshot and never performs asset I/O.
    void publish_prepared_assets(const PreparedAssets& prepared);

    /// Return the prepared snapshot only when it belongs to this manifest.
    /// The returned shared pointer keeps the immutable data alive for the
    /// complete graph-build/render invocation.
    [[nodiscard]] std::shared_ptr<const PreparedAssets>
    prepared_assets_for(const assets::AssetManifest& manifest) const;

    // ── WP-9 PR 9.0 / R1 — FontEngine slot ----------------------------
    /// RenderRuntime now OWNS the per-runtime FontEngine.  The accessor
    /// returns a reference because the engine is constructed during
    /// populate() and is never null for the lifetime of the runtime.
    [[nodiscard]] chronon3d::FontEngine& font_engine() const noexcept { return *m_font_engine_owned; }

    [[nodiscard]] chronon3d::cache::CacheDiagnostics&       diagnostics()       noexcept { return m_diagnostics; }
    [[nodiscard]] const chronon3d::cache::CacheDiagnostics& diagnostics() const noexcept { return m_diagnostics; }
    /// Whether the optional persistent framebuffer store was allocated for
    /// this runtime.  It is disabled by Config's
    /// `CHRONON_DISABLE_PERSISTENT_FB_CACHE` policy when set.
    [[nodiscard]] bool has_framebuffer_store() const noexcept {
        return static_cast<bool>(m_framebuffer_store);
    }
    /// Access the optional persistent store. Returns nullptr when the
    /// persistent framebuffer cache is disabled for this runtime.
    [[nodiscard]] chronon3d::cache::PersistentFramebufferStore* framebuffer_store() noexcept;
    [[nodiscard]] const chronon3d::cache::PersistentFramebufferStore* framebuffer_store() const noexcept;
    [[nodiscard]] chronon3d::cache::NodeCache&             node_cache()     noexcept { return m_owned_node_cache; }
    [[nodiscard]] chronon3d::graph::CompiledGraphCache&    graph_cache()    noexcept { return m_owned_graph_cache; }

    /// Formal compiled-topology domain facade. It is non-owning and returned
    /// by value so RenderRuntime remains the sole owner of the storage.
    /// Reset only compiled topology state. Frame values and temporal history
    /// remain untouched so callers can invalidate graph structure independently.
    void reset_compiled_cache();

    /// Reset only runtime-owned evaluated node values. The compiled topology
    /// and temporal history remain available for reuse.
    void reset_frame_value_cache();

    [[nodiscard]] std::shared_ptr<chronon3d::cache::FramebufferPool> framebuffer_pool_shared() noexcept { return m_owned_framebuffer_pool; }
    [[nodiscard]] chronon3d::cache::FramebufferPool&       framebuffer_pool() noexcept {
        return *m_owned_framebuffer_pool;
    }
    [[nodiscard]] chronon3d::graph::GraphExecutor&         executor()       noexcept { return *m_owned_executor; }

    [[nodiscard]] chronon3d::graph::GraphNodeCatalog&      graph_node_registry() noexcept { return *m_owned_graph_node_registry; }
    [[nodiscard]] chronon3d::effects::EffectCatalog&       effect_catalog() noexcept { return *m_owned_effect_catalog; }
    [[nodiscard]] chronon3d::ExecutionScheduler&           scheduler()      noexcept { return *m_scheduler; }
    [[nodiscard]] RenderSurfaceRegistry&                   surface_registry() noexcept { return m_surface_registry; }
    [[nodiscard]] const RenderSurfaceRegistry&             surface_registry() const noexcept { return m_surface_registry; }
    [[nodiscard]] GpuAssetCache&                           gpu_asset_cache() noexcept { return m_gpu_asset_cache; }
    [[nodiscard]] const GpuAssetCache&                     gpu_asset_cache() const noexcept { return m_gpu_asset_cache; }
    [[nodiscard]] GpuGlyphAtlas&                           gpu_glyph_atlas() noexcept { return m_gpu_glyph_atlas; }
    [[nodiscard]] const GpuGlyphAtlas&                     gpu_glyph_atlas() const noexcept { return m_gpu_glyph_atlas; }
    [[nodiscard]] GpuStyledGlyphCache&                     gpu_styled_glyph_cache() noexcept { return m_gpu_styled_glyph_cache; }
    [[nodiscard]] OverlayTemplateCache&                    overlay_template_cache() noexcept { return m_overlay_template_cache; }
    [[nodiscard]] const OverlayTemplateCache&              overlay_template_cache() const noexcept { return m_overlay_template_cache; }
    [[nodiscard]] GpuRuntime&                              gpu_runtime() noexcept { return m_gpu_runtime; }
    [[nodiscard]] const GpuRuntime&                        gpu_runtime() const noexcept { return m_gpu_runtime; }
    [[nodiscard]] MediaSessionPool&                        media_sessions() noexcept { return m_media_sessions; }
    [[nodiscard]] const MediaSessionPool&                  media_sessions() const noexcept { return m_media_sessions; }

    // WP-3 PR 3.1 — `scene_hasher()` + `program_store()` accessors were
    // REMOVED here.  Both state engines are now per-session owned; reach
    // them via `session.scene_hasher()` / `session.program_store()`
    // (or `session.common.scene_hasher()` / `program_store()` from a
    // `SoftwareRenderSession`).  See `docs/refactor-roadmap/03-render-session-boundary.md`.  // drift-class: historical (WP-3 design doc retired; rationale in render_session.hpp)

private:
    // Solely callable by RenderRuntime::create(RuntimeConfig).
    explicit RenderRuntime(chronon3d::Config config);

    // P1-14 — populate() + attach_backend() moved here from public.
    // populate() is called by the private Config constructor used by the
    // factory. attach_backend() is called by the two internal bridges via
    // friend.

    /// Initialise the long-lived infrastructure from the engine Config.
    /// Idempotent: calling populate() on a populated runtime is a no-op.
    /// After populate() the service-locator bundle is populated, the
    /// pipeline catalogs are wired, and the asset registry is initialised.
    /// The backend is NOT allocated here — see attach_backend().
    void populate();

    /// Attach a backend to the runtime.  Called by the higher-level
    /// orchestration (RenderEngine::Impl, runtime_adapter) which has
    /// access to the per-instance state (counters, settings) that
    /// lives on SoftwareRenderer.  Production code uses
    /// `RenderRuntime::create(RuntimeConfig)`; the backend is then
    /// attached via the internal bridges listed as `friend` below.
    void attach_backend(std::unique_ptr<chronon3d::graph::RenderBackend> backend);

    // Friend the two internal bridges that need access to attach_backend().
    // Both are forward-declared above the class.  This keeps the public
    // surface to a single entry (`create()`) while preserving the
    // established orchestration flow (runtime_adapter for production,
    // test_utils for tests).
    friend void ::chronon3d::backends::software::attach_software_backend(::chronon3d::SoftwareRenderer*);
    friend void ::chronon3d::backends::software::attach_software_backend(
        ::chronon3d::SoftwareRenderer*,
        ::chronon3d::graph::BackendPreference);
    friend void ::chronon3d::test::attach_software_backend(::chronon3d::SoftwareRenderer*);

    chronon3d::Config                                   m_config;
    chronon3d::graph::PipelineCatalogs                  m_catalogs;
    chronon3d::AssetRegistry                            m_assets;
    /// WP-8 PR 8.0 — typed asset resolver, sibling of m_assets; value
    /// member so lifetime is the runtime's, deterministic per engine.
    chronon3d::assets::AssetResolver                    m_resolver;
    chronon3d::assets::MeshPreparationCache              m_mesh_cache{};
    RenderSurfaceRegistry                                 m_surface_registry{};
    mutable std::mutex                                  m_prepared_assets_mutex;
    std::shared_ptr<const PreparedAssets>                m_prepared_assets{};
    std::string                                         m_prepared_mesh_manifest_key;

    // diag accessor: per-runtime CacheDiagnostics instance (value member; construction happens
    // at object-init time so even pre-populate() callers can use diagnostics() directly.
    // The friend declaration in cache_diagnostics.hpp gives RenderRuntime access to the
    // private default ctor.)
    // PLACEMENT: declared BEFORE every cache that registers with it so that
    // CacheDiagnostics outlives all registered caches during destruction.
    chronon3d::cache::CacheDiagnostics                  m_diagnostics{};
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
    chronon3d::CurveCache                           m_curve_cache;
    // Optional per-runtime persistent framebuffer store.  The CFB4 class and
    // codec remain independently constructible for tests, benchmarks, and
    // the future V3 tile cache; V1 does not pay for this subsystem when the
    // configured persistent cache is disabled.
    std::unique_ptr<chronon3d::cache::PersistentFramebufferStore> m_framebuffer_store;

    std::unique_ptr<chronon3d::graph::RenderBackend>   m_backend;
    GpuAssetCache                                      m_gpu_asset_cache{};
    GpuGlyphAtlas                                      m_gpu_glyph_atlas{};
    GpuStyledGlyphCache                                m_gpu_styled_glyph_cache{};
    OverlayTemplateCache                               m_overlay_template_cache{};
    GpuRuntime                                         m_gpu_runtime{};
    MediaSessionPool                                   m_media_sessions{};
    /// WP-9 PR 9.0 / R1 — runtime owns the per-runtime FontEngine.
    std::unique_ptr<chronon3d::FontEngine>            m_font_engine_owned;
    bool                                              m_populated{false};
};

// Fase B2 (DONE) — process_wide_assets_root() / process_wide_resolver() REMOVED.
// Production code must pass AssetResolver& through the call chain
// (RenderRuntime::resolver(), RenderSession, or dependency injection).
// Deep code without a runtime in scope should receive the resolver via
// parameter rather than reading a process-wide global.

} // namespace chronon3d::runtime
