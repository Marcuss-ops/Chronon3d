#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/assets/asset_registry.hpp>

namespace chronon3d {

// ── Internal helper (called once per constructor) ───────────────────────────

namespace {
    void init_renderer_defaults(SoftwareRenderer& renderer) {
        renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    }
}

// ── Construction ──────────────────────────────────────────────────────────────

RenderEngine::RenderEngine()
    : m_config(Config::from_environment())
    , m_assets()
    , m_renderer(std::make_unique<SoftwareRenderer>(std::move(m_config)))
{
    init_renderer_defaults(*m_renderer);
}

RenderEngine::RenderEngine(Config config)
    : m_config(std::move(config))
    , m_assets()
    , m_renderer(std::make_unique<SoftwareRenderer>(std::move(m_config)))
{
    init_renderer_defaults(*m_renderer);
}

RenderEngine::RenderEngine(Config config, std::filesystem::path assets_root)
    : m_config(std::move(config))
    , m_assets()
    , m_renderer(std::make_unique<SoftwareRenderer>(std::move(m_config)))
{
    init_renderer_defaults(*m_renderer);
    set_assets_root(assets_root);
}

RenderEngine::~RenderEngine() = default;

// ── Asset management ─────────────────────────────────────────────────────────

void RenderEngine::set_assets_root(const std::filesystem::path& root) {
    m_assets.mount(root);
    detail::set_default_assets_root(root.string());
}

// ── Backend injection ────────────────────────────────────────────────────────

void RenderEngine::set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
    m_renderer->set_image_backend(std::move(backend));
}

void RenderEngine::set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder) {
    m_renderer->set_video_decoder(std::move(decoder));
}

// ── Rendering ────────────────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height)
{
    return m_renderer->render_scene(scene, camera, width, height);
}

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera,
    i32 width, i32 height)
{
    return m_renderer->render_scene(scene, camera, width, height);
}

std::shared_ptr<Framebuffer> RenderEngine::render_frame(
    const Composition& comp, Frame frame)
{
    return m_renderer->render_frame(comp, frame);
}

} // namespace chronon3d
