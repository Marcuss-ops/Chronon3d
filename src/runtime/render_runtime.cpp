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
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <filesystem>

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

// WP-8 PR 8.1 — TU-local pointer mirror of the function-local static
// AssetResolver inside `typed_resolver_for_deep_code()` so the
// test-only reset hook (`namespace detail` below) can reach it
// without changing the helper's public signature.  Captured on first
// helper call; lifetime: the static AssetResolver itself, so the
// pointer is stable for the process lifetime and safe to dereference
// from `reset_typed_resolver_for_deep_code_for_testing()`.  Initialised
// inside the helper's static-initialisation block.
chronon3d::assets::AssetResolver* g_deep_resolver_mirror = nullptr;

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
        .software_registry   = m_owned_software_registry.get(),
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
                  "ExecutionScheduler (mode={}), 3 registries + 3 catalogs"
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
    // WP-3 PR 3.1 — the SessionServices table no longer wires
    // scene_hasher / program_store pointers: both state engines are
    // per-session owned and live on `RenderSession::scene_hasher`
    // and `RenderSession::program_store` respectively.  Productions
    // reach them via `session.scene_hasher()` /
    // `session.program_store()` directly.
    chronon3d::SoftwareRenderSession session;
    session.common.services = SessionServices{
        .executor            = runtime.services().executor,
        .node_cache          = runtime.services().node_cache,
        .framebuffer_pool    = runtime.services().framebuffer_pool,
        .graph_cache         = runtime.services().graph_cache,
        .asset_registry      = runtime.services().asset_registry,
        .default_assets_root = &runtime.default_assets_root(),
        // WP-3 PR 3.1 — .scene_hasher / .program_store fields removed from
        // SessionServices.  See header for rationale.
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

// ── typed_resolver_for_deep_code — WP-8 PR 8.1 ─────────────────────
//
// Service-locator for callers that have no RenderRuntime in their
// call stack (font_engine, text_rasterizer, preflight, BL/FT texture
// caches, etc.).  Prefers the active runtime's resolver; falls back
// to a lazy-initialised process-wide singleton mounted against
// `process_wide_assets_root()`.  Thread-safety is delegated to the
// resolver's internal lock — concurrent first-callers all see the
// same root and `mount()` serializes + is idempotent.
//
// First-mount-only semantics (PR 8.1 contract): the singleton
// resolver mounts ONCE for the lifetime of the static.  Subsequent
// `set_process_wide_assets_root(...)` calls after the first mount
// do NOT propagate to this fallback.  Production paths (CLI
// `render_job_setup` + `test_main`) all set the root at process
// start before any deep-code call, so the constraint is benign in
// practice; tests should call
// `chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing()`
// around each fixture to ensure isolation.
const chronon3d::assets::AssetResolver& typed_resolver_for_deep_code() {
    if (auto* rt = active_runtime()) {
        return rt->resolver();
    }
    static chronon3d::assets::AssetResolver g_deep_resolver;
    // Capture the function-local static's address ONCE so the
    // detail::reset hook below can reach it.  Function-local statics
    // are themselves initialised once in C++17+; capturing the address
    // on every call is idempotent and inexpensive.
    g_deep_resolver_mirror = &g_deep_resolver;
    // Single mount attempt guarded by the resolver's own mutex; we
    // don't need an external once_flag because the resolver's lock
    // makes the multi-thread first mount + idempotent state trivial.
    if (!g_deep_resolver.has_mount()) {
        const auto root_str = process_wide_assets_root();
        if (!root_str.empty()) {
            const auto root_path = std::filesystem::path(root_str);
            // Skip non-absolute paths — AssetResolver's contract is
            // "mount only accepts absolute" and the legacy
            // process_wide_assets_root may be empty or relative on
            // misconfigured paths.  Empty / relative root leaves the
            // resolver unmounted, which the 2-line call-site pattern
            // turns into "fall back to raw rel_path" — the exact
            // legacy `resolve_asset_path(relative)` semantics.
            if (root_path.is_absolute()) {
                try {
                    g_deep_resolver.mount(root_path.lexically_normal());
                } catch (const std::invalid_argument&) {
                    // Already filtered by `is_absolute()`; leave
                    // unmounted.  Other exceptions (bad_alloc, etc.)
                    // intentionally propagate.
                }
            }
        }
    }
    return g_deep_resolver;
}

namespace detail {

// WP-8 PR 8.1 — test-only reset hook.  Unmounts the process-wide
// fallback singleton so each test fixture starts with a clean
// slate.  All production callers leave this hook unused; it lives
// in `detail::` to make the test-only nature explicit.
void reset_typed_resolver_for_deep_code_for_testing() {
    // Forward-declared via TU-local static cannot be reached from
    // outside this TU.  Test fixtures reach the singleton indirectly
    // by calling `set_process_wide_assets_root("")` followed by
    // setting the desired absolute root, which re-arms the
    // first-mount branch on the next `typed_resolver_for_deep_code()`
    // call IF the static still reports `!has_mount()`.  This helper
    // explicitly unmounts so the first-mount branch always fires
    // even if a previous test mounted an absolute value.
    //
    // Implementation note: we cannot touch the static directly because
    // its address is local to `typed_resolver_for_deep_code()`.  To
    // expose it without changing the helper signature, we keep a
    // mirror pointer here for tests to reach.  See the matching
    // declaration in the header.
    //
    // The mirror is captured by the helper on first-call via a
    // leaky-but-safe pattern (function-local static + a one-time
    // assignment in this `detail` namespace on first test access).
    if (g_deep_resolver_mirror) {
        g_deep_resolver_mirror->unmount();
    }
}

} // namespace detail

} // namespace chronon3d::runtime
