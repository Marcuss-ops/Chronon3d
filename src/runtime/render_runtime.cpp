// ===========================================================================
// runtime/render_runtime.cpp
//
// TICKET-011 — RenderRuntime implementation.  Sole owner of engine-
// lifetime state:
//
//   - All caches (NodeCache, FramebufferPool, CompiledGraphCache)
//   - All catalogs (PipelineCatalogs → graph_nodes + effects +
//     extensions + precomp_builder)
//   - All registries (GraphNodeCatalog, EffectCatalog)
//   - GraphExecutor + ExecutionScheduler
//
// The backend slot is attached externally by RenderEngine::Impl because
// SoftwareBackend's ctor needs RenderCounters & + RenderSettings & that
// live on SoftwareRenderer.
//
// WP-3 PR 3.1 — `SceneHasher` and `SceneProgramStore` are no longer
// runtime-owned.  They were relocated from RenderSession to RenderRuntime
// in WP-8 (see prior revisions) and are now back per-session-owned.
// The runtime therefore no longer populates them, no longer assigns
// them into the service bundle, and no longer wires them into the
// SessionServices table via `make_session()`.  The corresponding
// service-bundle fields in `RenderServices` are gone (see header).
//
// ===========================================================================

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <filesystem>

namespace chronon3d::runtime {

namespace {

// Process-wide fallback assets root for contexts that don't construct
// a RenderRuntime (CLI tests, content-module test fixtures).  Replaces
// the legacy `chronon3d::detail::g_default_assets_root`.  Writers:
//   - runtime::set_process_wide_assets_root (CLI / tests + engine init)
std::mutex g_process_root_mutex;
std::string g_process_wide_assets_root;

} // namespace

RenderRuntime::RenderRuntime(chronon3d::Config config)
    : m_config(std::move(config))
{
    populate();
}

RenderRuntime::~RenderRuntime() = default;

void RenderRuntime::populate() {
    if (m_populated) {
        return;
    }
    const auto& cache_cfg = m_config.cache();
    const auto& sched_cfg = m_config.scheduler();

    // ── Long-lived caches + executor + catalogs + scheduler ─────────
    m_owned_node_cache = cache::NodeCache{cache_cfg.node_cache_max_bytes()};
    m_owned_framebuffer_pool =
        std::make_shared<cache::FramebufferPool>(cache_cfg.fb_pool_max_bytes());
    m_owned_executor    = std::make_unique<chronon3d::graph::GraphExecutor>();
    m_owned_graph_node_registry  = std::make_unique<chronon3d::graph::GraphNodeCatalog>();
    m_owned_effect_catalog       = std::make_unique<chronon3d::effects::EffectCatalog>();
    m_scheduler                  = std::make_unique<chronon3d::ExecutionScheduler>(
        make_execution_scheduler(m_config));
    // WP-3 PR 3.1 — no longer populating m_owned_scene_hasher /
    // m_owned_program_store here: both are per-session owned in the
    // WP-3 PR 3.1 architecture.

    // ── Service bundle: typed pointer view of long-lived state ───────
    m_services = RenderServices{
        .asset_registry      = &m_assets,
        .asset_resolver      = &m_resolver,
        .node_cache          = &m_owned_node_cache,
        .framebuffer_pool    = m_owned_framebuffer_pool.get(),
        .graph_cache         = &m_owned_graph_cache,
        .scheduler           = m_scheduler.get(),
        .executor            = m_owned_executor.get(),
        .graph_node_registry = m_owned_graph_node_registry.get(),
        .effect_catalog      = m_owned_effect_catalog.get(),
        // WP-3 PR 3.1 — .scene_hasher and .program_store fields are
        // gone from RenderServices (per-session ownership).  This is
        // the canonical PR 3.1 service-bundle table.
    };

    // ── Populate builtin processors/effects + freeze the catalogs ───
    // TICKET-011 — these registrations previously lived in
    // SoftwareRenderer's ctor body.  They are engine-lifetime state so
    // they belong here.  The renderer borrows the populated registries
    // and the frozen EffectCatalog via m_runtime->graph_node_registry()
    // / m_runtime->effect_catalog().
    // Fase 4 — SoftwareRegistry ownership moved to SoftwareRenderer;
    // builtin processors are registered there via
    // backends::software::register_builtin_processors().
    graph::register_pipeline_graph_nodes(*m_owned_graph_node_registry);
    m_owned_effect_catalog->freeze();

    // ── Backend slot: deferred to attach_backend() ───────────────────
    // Producing SoftwareBackend requires the renderer's per-instance
    // RenderCounters & + RenderSettings & + the scheduler above, so
    // construction lives in RenderEngine::Impl after SoftwareRenderer
    // is ready.  See header comment.

    // ── Mount default assets into the runtime's AssetRegistry + the
    //    typed AssetResolver (PR 8.0 sibling that PR 8.1 routes
    //    consumers to) ───────────────────────────────────────────────
    m_assets.mount(std::filesystem::path{});
    m_resolver.mount(std::filesystem::path{});

    m_populated = true;
    spdlog::debug("RenderRuntime::populate(): runtime populated with "
                  "NodeCache({}B), FramebufferPool({}B), GraphExecutor, "
                  "ExecutionScheduler (mode={}), 2 registries + 3 catalogs"
                  " (WP-3 PR 3.1: scene_hasher + program_store are per-session owned,"
                  " NOT runtime-owned)",
                  cache_cfg.node_cache_max_bytes(),
                  cache_cfg.fb_pool_max_bytes(),
                  static_cast<int>(sched_cfg.mode()));
}

void RenderRuntime::attach_backend(
    std::unique_ptr<chronon3d::graph::RenderBackend> backend)
{
    if (!backend) {
        throw std::invalid_argument(
            "RenderRuntime::attach_backend(): nullptr");
    }
    if (m_backend) {
        throw std::runtime_error(
            "RenderRuntime::attach_backend(): backend already attached");
    }
    m_backend = std::move(backend);
}

// WP-0 PR 0.1 — `noexcept` REMOVED from both overloads: the body
// throws std::runtime_error on an unattached backend.  Previously
// declared `noexcept`, this would have called std::terminate instead
// of surfacing the error.  The header mirrors this no-noexcept
// declaration; see check [11/12] of
// `tools/check_architecture_boundaries.sh` for the regression guard.
chronon3d::graph::RenderBackend& RenderRuntime::backend() {
    if (!m_backend) {
        throw std::runtime_error(
            "RenderRuntime::backend() called before attach_backend().");
    }
    return *m_backend;
}

const chronon3d::graph::RenderBackend& RenderRuntime::backend() const {
    if (!m_backend) {
        throw std::runtime_error(
            "RenderRuntime::backend() (const) called before attach_backend().");
    }
    return *m_backend;
}

void set_process_wide_assets_root(std::string root) {
    std::lock_guard<std::mutex> lock(g_process_root_mutex);
    g_process_wide_assets_root = std::move(root);
}

// Return the process-wide fallback assets root by value (NOT by
// reference) so callers cannot hold a pointer into the mutex-guarded
// module-level string past the lock release — a concurrent
// `set_process_wide_assets_root` would move-assign the underlying
// string and invalidate any external reference.  Returns an empty
// string when no fallback has been configured.
//
// (Cost: one std::string copy.  Called on deep-code asset resolution
// which is not hot enough to warrant cleverness.)
std::string process_wide_assets_root() {
    std::lock_guard<std::mutex> lock(g_process_root_mutex);
    return g_process_wide_assets_root;
}

// WP-8 PR 8.1 Final — process-wide typed asset resolver for deep code
// that has no RenderRuntime in scope (CLI dev paths, content-layer
// ergonomics, etc.).  Same lazy-static + first-mount semantics as the
// legacy `typed_resolver_for_deep_code()` minus the active-runtime
// branch (which lived on the deleted `runtime::set_active_runtime()`
// channel).
//
// Resolver serialises concurrent first-callers through its internal
// shared_mutex; idempotent mount state so the multi-thread first
// call is safe without an external once_flag.  Lifetime is the
// process.
const chronon3d::assets::AssetResolver& process_wide_resolver() {
    static chronon3d::assets::AssetResolver g_process_wide_resolver;
    if (!g_process_wide_resolver.has_mount()) {
        const auto root_str = process_wide_assets_root();
        if (!root_str.empty()) {
            const auto root_path = std::filesystem::path(root_str);
            // AssetResolver::mount contract: only absolute paths.
            if (root_path.is_absolute()) {
                try {
                    g_process_wide_resolver.mount(root_path.lexically_normal());
                } catch (const std::invalid_argument&) {
                    // Leave unmounted; call-site 2-line pattern treats
                    // unmounted as "fall back to raw rel_path".
                }
            }
        }
    }
    return g_process_wide_resolver;
}

} // namespace chronon3d::runtime
