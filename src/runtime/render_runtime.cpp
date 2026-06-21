// ===========================================================================
// runtime/render_runtime.cpp
//
// TICKET-011 — RenderRuntime implementation.  Sole owner of engine-
// lifetime state:
//
//   - All caches (NodeCache, FramebufferPool, CompiledGraphCache)
//   - All catalogs (PipelineCatalogs → graph_nodes + effects +
//     extensions + precomp_builder)
//   - All registries (SoftwareRegistry, GraphNodeCatalog, EffectCatalog)
//   - GraphExecutor + ExecutionPlanCache + ExecutionScheduler
//
// The backend slot is attached externally by RenderEngine::Impl because
// SoftwareBackend's ctor needs RenderCounters & + RenderSettings & that
// live on SoftwareRenderer.
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
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace chronon3d::runtime {

namespace {

// Process-wide most-recently-attached runtime pointer.  Used by deep
// rendering code that has no direct link to a RenderRuntime in its
// call stack to find the engine's default assets root.
// Single writer (engine init / destruction); concurrent reads allowed.
std::atomic<RenderRuntime*> g_active_runtime{nullptr};

// Process-wide fallback assets root for contexts that don't construct
// a RenderRuntime (CLI tests, content-module test fixtures).  Replaces
// the legacy `chronon3d::detail::g_default_assets_root`.  Writers:
//   - RenderRuntime::set_default_assets_root (engine init)
//   - runtime::set_process_wide_assets_root (CLI / tests)
std::mutex g_process_root_mutex;
std::string g_process_wide_assets_root;

} // namespace

RenderRuntime::RenderRuntime(chronon3d::Config config)
    : m_config(std::move(config))
{
    populate();
}

RenderRuntime::~RenderRuntime() {
    // Drop the active-runtime pointer if we owned it so deep code
    // doesn't see a dangling runtime after destruction.
    RenderRuntime* expected = this;
    g_active_runtime.compare_exchange_strong(expected, nullptr);
}

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
    m_owned_software_registry     = std::make_unique<chronon3d::renderer::SoftwareRegistry>();
    m_owned_graph_node_registry  = std::make_unique<chronon3d::graph::GraphNodeCatalog>();
    m_owned_effect_catalog       = std::make_unique<chronon3d::effects::EffectCatalog>();
    m_scheduler                  = std::make_unique<chronon3d::ExecutionScheduler>(
        make_execution_scheduler(m_config));
    // WP-8 follow-up: scene_hasher is a default-constructible value
    // member (no explicit construction needed).  program_store uses
    // unique_ptr because SceneProgramStore carries a std::mutex and
    // is therefore non-movable; the runtime is the sole owner.
    m_owned_program_store = std::make_unique<chronon3d::graph::SceneProgramStore>();

    // ── Service bundle: typed pointer view of long-lived state ───────
    m_services = RenderServices{
        .asset_registry      = &m_assets,
        .asset_resolver      = &m_resolver,
        .node_cache          = &m_owned_node_cache,
        .framebuffer_pool    = m_owned_framebuffer_pool.get(),
        .graph_cache         = &m_owned_graph_cache,
        .scheduler           = m_scheduler.get(),
        .executor            = m_owned_executor.get(),
        .software_registry   = m_owned_software_registry.get(),
        .graph_node_registry = m_owned_graph_node_registry.get(),
        .effect_catalog      = m_owned_effect_catalog.get(),
        .scene_hasher        = &m_owned_scene_hasher,
        .program_store       = m_owned_program_store.get(),
    };

    // ── Populate builtin processors/effects + freeze the catalogs ───
    // TICKET-011 — these registrations previously lived in
    // SoftwareRenderer's ctor body.  They are engine-lifetime state so
    // they belong here.  The renderer borrows the populated registries
    // and the frozen EffectCatalog via m_runtime->software_registry()
    // / m_runtime->graph_node_registry() / m_runtime->effect_catalog().
    renderer::register_builtin_processors(*m_owned_software_registry);
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

    // ── Publish as "active" so deep code can find us ────────────────
    set_active_runtime(this);

    m_populated = true;
    spdlog::debug("RenderRuntime::populate(): runtime populated with "
                  "NodeCache({}B), FramebufferPool({}B), GraphExecutor, "
                  "ExecutionScheduler (mode={}), 3 registries + 3 catalogs",
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

chronon3d::graph::RenderBackend& RenderRuntime::backend() noexcept {
    if (!m_backend) {
        throw std::runtime_error(
            "RenderRuntime::backend() called before attach_backend().");
    }
    return *m_backend;
}

const chronon3d::graph::RenderBackend& RenderRuntime::backend() const noexcept {
    if (!m_backend) {
        throw std::runtime_error(
            "RenderRuntime::backend() (const) called before attach_backend().");
    }
    return *m_backend;
}

void RenderRuntime::set_default_assets_root(std::string root) {
    m_default_assets_root = std::move(root);
    if (!m_default_assets_root.empty()) {
        const auto root_path = std::filesystem::path(m_default_assets_root);
        m_assets.mount(root_path);
        // PR 8.0 — mirror to the typed resolver sibling so future
        // PR 8.1 consumers can reach a runtime-owned resolver.  The
        // mirror is intentional duplication; legacy free functions
        // (asset_registry::resolve_path, runtime::resolve_asset_path)
        // continue to work via m_assets until PR 8.1 migrates them.
        m_resolver.mount(root_path);
    }
    // Mirror to the process-wide fallback so deep code that loses
    // the runtime ref still resolves the same root.
    set_process_wide_assets_root(m_default_assets_root);
    set_active_runtime(this);
}

void set_active_runtime(RenderRuntime* runtime) {
    g_active_runtime.store(runtime, std::memory_order_release);
}

RenderRuntime* active_runtime() {
    return g_active_runtime.load(std::memory_order_acquire);
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

std::string default_assets_root_for_deep_code() {
    // TICKET-011a follow-up #2 — the legacy fallback to
    // `chronon3d::detail::g_default_assets_root` is gone; the only
    // out-of-runtime source is the typed `g_process_wide_assets_root`
    // mirrored on every RenderRuntime::set_default_assets_root call,
    // and settable by CLI/test init via `set_process_wide_assets_root`.
    if (auto* rt = active_runtime()) {
        return rt->default_assets_root();
    }
    return process_wide_assets_root();
}

[[nodiscard]] chronon3d::SoftwareRenderSession
make_session(RenderRuntime& runtime) {
    // TICKET-011a follow-up #1 — wire back-pointers from the runtime
    // into the returned session via a SessionServices table.  The
    // mapping is by-value (one record per session, copy-on-make) so
    // sessions don't need a global lookup table; the back-pointers
    // are RAW pointers (non-owning) with lifetime backed by the
    // runtime itself.
    chronon3d::SoftwareRenderSession session;
    session.common.services = SessionServices{
        .executor            = runtime.services().executor,
        .node_cache          = runtime.services().node_cache,
        .framebuffer_pool    = runtime.services().framebuffer_pool,
        .graph_cache         = runtime.services().graph_cache,
        .asset_registry      = runtime.services().asset_registry,
        .default_assets_root = &runtime.default_assets_root(),
        // WP-8 follow-up: scope-scoped scene-hasher + program-store
        // back-pointers into the session so its accessor methods (in
        // src/runtime/render_session.cpp) can reach the runtime-owned
        // state through `services` instead of via header include.
        .scene_hasher        = runtime.services().scene_hasher,
        .program_store       = runtime.services().program_store,
    };
    return session;
}

const SessionServices& session_services(const chronon3d::SoftwareRenderSession& session) {
    // The session carries its bound services on `common.services`
    // (since `SoftwareRenderSession` is a composition of
    // `common` = `RenderSession` + `software` = `SoftwareSessionResources`).
    //
    // Always return the field directly (never a static empty record).
    // A default-constructed session returns a stable referent whose
    // individual pointer fields are simply null; callers null-check.
    // Returning the field unconditionally preserves legitimate
    // partial-population bindings from test fixtures.
    return session.common.services;
}

} // namespace chronon3d::runtime
