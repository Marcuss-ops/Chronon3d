// ===========================================================================
// api/render_engine.cpp — RenderEngine::Impl wired for TICKET-011
//
// Construction sequence inside `Impl`:
//   1) m_runtime(m_config)       — populate(): allocates caches, pool,
//                                  executor, plan cache, scheduler,
//                                  3 registries, 3 catalogs (but NOT the
//                                  backend)
//   2) m_renderer(m_runtime, cfg) — renderer holds per-instance state
//                                  (counters, settings, image backend,
//                                  video decoder, session, registry)
//                                  and BORROWS services from the runtime
//   3) m_runtime.attach_backend(...) — SoftwareBackend is constructed
//                                  externally because its ctor takes the
//                                  renderer's per-instance counters & +
//                                  settings &, plus the runtime-owned
//                                  framebuffer pool.  After attach the
//                                  runtime is the sole owner of the
//                                  backend slot for the engine lifetime.
//
// WP-8 PR 8.1 Final — `set_assets_root` mounts the root into the
// runtime's typed AssetResolver sibling + the legacy AssetRegistry,
// and mirrors it to the process-wide slot via
// `runtime::set_process_wide_assets_root`.  The retired orphan
// `RenderRuntime::default_assets_root()` accessor and the retired
// bridge `runtime::default_assets_root_for_deep_code()` both routed
// through this slot (the process-wide slot is the single source of
// truth, surfaced via `runtime::process_wide_assets_root()`).
// ===========================================================================

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/assets/asset_registry.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <utility>

namespace chronon3d {

struct RenderEngine::Impl {
    /// Pass C — friend declaration for `chronon3d::advanced::RenderEngineAccess`.
    /// Grants the advanced accessor direct access to the Impl internals so
    /// legacy call sites can keep reaching the renderer / runtime during the
    /// Pass B→D migration window.  Removed in V0.2 freeze (Pass D).
    friend class ::chronon3d::advanced::RenderEngineAccess;

    Config                                       m_config;
    AssetRegistry                                m_assets;
    runtime::RenderRuntime                       m_runtime;
    std::unique_ptr<SoftwareRenderer>            m_renderer;
    // RenderPipeline needs both the renderer (backend) and the runtime
    // (services).  Both are constructed before the pipeline is emplace()d
    // in the body, so std::optional is the right storage here.
    std::optional<runtime::RenderPipeline>       m_pipeline;

    explicit Impl(Config config)
        : m_config(std::move(config))
        , m_runtime(m_config)        // RenderRuntime ctor calls populate()
        , m_renderer(std::make_unique<SoftwareRenderer>(m_runtime, m_config))
    {
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        // TICKET-011 — build + attach the SoftwareBackend now that
        // the renderer exists.  Construction parameters:
        //   - renderer's per-instance counters (live on m_renderer)
        //   - renderer's per-instance settings (live on m_renderer)
        //   - runtime-owned FramebufferPool shared_ptr
        // After attach_backend() the runtime is sole owner of the backend.
        m_runtime.attach_backend(std::make_unique<SoftwareBackend>(
            m_renderer.get(),            // 06 R3b owner back-pointer
            *m_renderer->counters(),
            m_renderer->render_settings(),
            m_runtime.framebuffer_pool_shared()));

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(m_renderer.get(), m_runtime);

        spdlog::debug("RenderEngine::Impl: constructed; runtime backend attached");
    }

    Impl(Config config, std::filesystem::path assets_root)
        : m_config(std::move(config))
        , m_runtime(m_config)        // populate() — no longer publishes an active-runtime pointer (WP-8 PR 8.1)
        , m_renderer(std::make_unique<SoftwareRenderer>(m_runtime, m_config))
    {
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        m_runtime.attach_backend(std::make_unique<SoftwareBackend>(
            m_renderer.get(),            // 06 R3b owner back-pointer
            *m_renderer->counters(),
            m_renderer->render_settings(),
            m_runtime.framebuffer_pool_shared()));

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(m_renderer.get(), m_runtime);

        set_assets_root(assets_root);   // mounts root + mirrors to process-wide slot (WP-8 PR 8.1)
        spdlog::debug("RenderEngine::Impl: constructed with assets_root={}",
                      assets_root.string());
    }

    ~Impl() = default;

    void set_assets_root(const std::filesystem::path& root) {
        const std::string root_str = root.string();
        m_assets.mount(root);                                         // legacy AssetRegistry mount (for non-resolver consumers)
        m_runtime.resolver().mount(root);                             // WP-8 PR 8.0 sibling resolver, mounted inside the runtime
        runtime::set_process_wide_assets_root(root_str);              // mirror to process-wide slot for deep code without a runtime in scope
    }

    chronon3d::SoftwareRenderSession create_session() {
        return runtime::make_session(m_runtime);
    }
};

// ── Construction / move specials ─────────────────────────────────────────

RenderEngine::RenderEngine()
    : m_impl(std::make_unique<Impl>(Config::from_environment()))
{}

RenderEngine::RenderEngine(Config config)
    : m_impl(std::make_unique<Impl>(std::move(config)))
{}

RenderEngine::RenderEngine(Config config, std::filesystem::path assets_root)
    : m_impl(std::make_unique<Impl>(std::move(config), assets_root))
{}

RenderEngine::~RenderEngine() = default;

RenderEngine::RenderEngine(RenderEngine&& other) noexcept
    : m_impl(std::move(other.m_impl))
{}

RenderEngine& RenderEngine::operator=(RenderEngine&& other) noexcept {
    if (this != &other) m_impl = std::move(other.m_impl);
    return *this;
}

// ── Asset management ───────────────────────────────────────────────────────

void RenderEngine::set_assets_root(const std::filesystem::path& root) {
    m_impl->set_assets_root(root);
}

AssetRegistry& RenderEngine::assets() noexcept { return m_impl->m_assets; }
const AssetRegistry& RenderEngine::assets() const noexcept { return m_impl->m_assets; }

std::string RenderEngine::assets_root() const noexcept {
    // WP-8 PR 8.1 Final — read from the process-wide slot (the
    // single source of truth after the orphan `default_assets_root`
    // field was retired).  Returns by value so callers cannot hold a
    // reference past a concurrent `set_assets_root()` from another
    // thread.
    return runtime::process_wide_assets_root();
}

// ── Composition registry ───────────────────────────────────────────────────

void RenderEngine::set_composition_registry(const CompositionRegistry* registry) {
    m_impl->m_renderer->set_composition_registry(registry);
}

// ── Rendering (forward) ────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height)
{
    return m_impl->m_pipeline->render_scene(scene, camera, width, height);
}

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera,
    i32 width, i32 height)
{
    return m_impl->m_pipeline->render_scene(scene, camera, width, height);
}

std::shared_ptr<Framebuffer> RenderEngine::render_frame(
    const Composition& comp, Frame frame)
{
    return m_impl->m_pipeline->render_composition(comp, frame);
}

// ── Backend injection ─────────────────────────────────────────────────────

void RenderEngine::set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
    m_impl->m_renderer->set_image_backend(std::move(backend));
}

void RenderEngine::set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder) {
    m_impl->m_renderer->set_video_decoder(std::move(decoder));
}

// ── Settings ──────────────────────────────────────────────────────────────

void RenderEngine::set_settings(const RenderSettings& settings) {
    m_impl->m_renderer->set_settings(settings);
}

const RenderSettings& RenderEngine::settings() const noexcept {
    return m_impl->m_renderer->render_settings();
}

// ── Accessors ─────────────────────────────────────────────────────────────

// (Pass C — 2026-06-29)  The public accessors `RenderEngine::renderer()`
// (×2), `RenderEngine::runtime()` (×2), and `RenderEngine::create_session()`
// were moved out of the public RenderEngine surface.  Their functionality
// is now reached exclusively through
// `chronon3d::advanced::RenderEngineAccess` from
// `<chronon3d/advanced/render_engine_access.hpp>`.  The declarations in
// `include/chronon3d/api/render_engine.hpp` were removed in the same
// commit; the corresponding definitions are gone from this TU.
// The replacement bodies are appended at the bottom of this file under
// `namespace chronon3d::advanced` and access Impl through the friend
// declaration declared above.

const Config& RenderEngine::config() const noexcept {
    return m_impl->m_config;
}

void RenderEngine::clear_caches() { m_impl->m_renderer->clear_caches(); }
void RenderEngine::reset_counters() { m_impl->m_renderer->reset_counters(); }

// (Pass C)  `RenderEngine::create_session()` moved to
// `chronon3d::advanced::RenderEngineAccess::create_session()` (below).

} // namespace chronon3d

// ═══════════════════════════════════════════════════════════════════════════
// chronon3d::advanced::RenderEngineAccess — bodies live here because they
// need `RenderEngine::Impl` to be a complete type (it is, in this TU).
// Forward declared in `<chronon3d/advanced/render_engine_access.hpp>`.
// The `friend class ::chronon3d::advanced::RenderEngineAccess;` declaration
// in `struct RenderEngine::Impl` (above) is what authorises the access to
// private members `m_runtime`, `m_renderer`, and the inner
// `create_session()`.  `m_impl` (a private member of `RenderEngine`) is
// reachable via the friend declaration already present in
// `include/chronon3d/api/render_engine.hpp` (added in P1-B).
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::advanced {

runtime::RenderRuntime& RenderEngineAccess::runtime(RenderEngine& engine) noexcept {
    return engine.m_impl->m_runtime;
}

SoftwareRenderer& RenderEngineAccess::software_renderer(RenderEngine& engine) noexcept {
    return *engine.m_impl->m_renderer;
}

chronon3d::SoftwareRenderSession RenderEngineAccess::create_session(RenderEngine& engine) {
    return engine.m_impl->create_session();
}

} // namespace chronon3d::advanced
