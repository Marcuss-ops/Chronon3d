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
// `set_assets_root` writes the runtime-local default_assets_root + mounts it
// into the runtime's AssetRegistry.  The legacy process-wide global is
// NOT mutated from this path; deep rendering helpers consult
// `runtime::default_assets_root_for_deep_code()` which prefers the
// active runtime and falls back to the legacy global.
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
                                    // which already set_active_runtime(this)
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
            *m_renderer->counters(),
            m_renderer->settings(),
            m_runtime.framebuffer_pool_shared()));

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(*m_renderer, m_runtime);

        spdlog::debug("RenderEngine::Impl: constructed; runtime backend attached");
    }

    Impl(Config config, std::filesystem::path assets_root)
        : m_config(std::move(config))
        , m_runtime(m_config)        // populate() set_active_runtime(this)
        , m_renderer(std::make_unique<SoftwareRenderer>(m_runtime, m_config))
    {
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        m_runtime.attach_backend(std::make_unique<SoftwareBackend>(
            *m_renderer->counters(),
            m_renderer->settings(),
            m_runtime.framebuffer_pool_shared()));

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(*m_renderer, m_runtime);

        set_assets_root(assets_root);   // also wires active_runtime
        spdlog::debug("RenderEngine::Impl: constructed with assets_root={}",
                      assets_root.string());
    }

    ~Impl() = default;

    void set_assets_root(const std::filesystem::path& root) {
        const std::string root_str = root.string();
        m_assets.mount(root);
        m_runtime.set_default_assets_root(root_str);
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

const std::string& RenderEngine::assets_root() const noexcept {
    return m_impl->m_runtime.default_assets_root();
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
    return m_impl->m_renderer->settings();
}

// ── Accessors ─────────────────────────────────────────────────────────────

SoftwareRenderer&       RenderEngine::renderer() noexcept       { return *m_impl->m_renderer; }
const SoftwareRenderer& RenderEngine::renderer() const noexcept { return *m_impl->m_renderer; }

runtime::RenderRuntime&       RenderEngine::runtime() noexcept       { return m_impl->m_runtime; }
const runtime::RenderRuntime& RenderEngine::runtime() const noexcept { return m_impl->m_runtime; }

const Config& RenderEngine::config() const noexcept {
    return m_impl->m_config;
}

void RenderEngine::clear_caches() { m_impl->m_renderer->clear_caches(); }
void RenderEngine::reset_counters() { m_impl->m_renderer->reset_counters(); }

chronon3d::SoftwareRenderSession RenderEngine::create_session() {
    return m_impl->create_session();
}

} // namespace chronon3d
