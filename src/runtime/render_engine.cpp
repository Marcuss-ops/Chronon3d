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
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>  // Fase A2 — attach_software_backend factory
#include <chronon3d/assets/asset_registry.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <utility>

namespace chronon3d {

struct RenderEngine::Impl {
    // P1-F Pass D — the friend declaration for
    // `chronon3d::advanced::RenderEngineAccess` was REMOVED here in lockstep
    // with the deletion of `advanced/render_engine_access.hpp` and the
    // corresponding accessor bodies at the bottom of this TU.  OPP-internal
    // access to the SoftwareRenderer is now reached via the public-path
    // `RenderEngine::render()` canonical entry (no more escape hatch).

    Config                                       m_config;
    AssetRegistry                                m_assets;
    std::unique_ptr<runtime::RenderRuntime>    m_runtime;
    std::unique_ptr<SoftwareRenderer>            m_renderer;
    // RenderPipeline needs both the renderer (backend) and the runtime
    // (services).  Both are constructed before the pipeline is emplace()d
    // in the body, so std::optional is the right storage here.
    std::optional<runtime::RenderPipeline>       m_pipeline;

    // Fase C2 / R1 — unified constructor delegates runtime assembly to
    // the shared internal `assemble_runtime()` helper.
    explicit Impl(Config config, std::optional<std::filesystem::path> assets_root = std::nullopt)
        : m_config(std::move(config))
    {
        auto runtime_result = runtime::detail::assemble_runtime(
            runtime::RuntimeConfig{.config = m_config, .assets_root = assets_root});
        if (!runtime_result) {
            throw std::runtime_error(
                "RenderEngine::Impl: runtime assembly failed: " +
                runtime_result.error().message);
        }
        m_runtime = std::move(runtime_result).value();
        m_renderer = std::make_unique<SoftwareRenderer>(*m_runtime, m_config);

        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        // Fase A2 — unify backend construction through the canonical
        // `attach_software_backend()` factory (runtime_adapter.hpp).
        // This replaces the previously-inlined services bundle +
        // make_software_backend + attach_processor_context sequence
        // that was duplicated across 3 files.
        chronon3d::backends::software::attach_software_backend(m_renderer.get());

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(m_renderer.get(), *m_runtime);

        if (assets_root.has_value()) {
            set_assets_root(*assets_root);
            spdlog::debug("RenderEngine::Impl: constructed with assets_root={}",
                          assets_root->string());
        } else {
            spdlog::debug("RenderEngine::Impl: constructed; runtime backend attached");
        }
    }

    ~Impl() = default;

    void set_assets_root(const std::filesystem::path& root) {
        m_runtime->resolver().mount(root);                            // WP-8 PR 8.0 sibling resolver, mounted inside the runtime
        // AssetRegistry no longer holds a mount root; path resolution
        // is the resolver's job.
    }

    // P1-F Pass D — `create_session()` (private helper for the legacy
    // RenderEngine::create_session() public + advanced::RenderEngineAccess
    // escape hatch) is REMOVED.  No callers remain after the Pass D deletes
    // both that public method and the RenderEngineAccess accessor.
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
    // P1 #7 — return the engine-local resolver's mount root (per-
    // runtime), NOT the process-wide global.  Two engines with
    // different asset roots now observe their own value.
    // Returns by value so callers cannot hold a reference past a
    // concurrent `set_assets_root()` from another thread.
    return m_impl->m_runtime->resolver().mount_root().string();
}

// ── Composition registry ───────────────────────────────────────────────────

void RenderEngine::set_composition_registry(const CompositionRegistry* registry) {
    m_impl->m_renderer->set_composition_registry(registry);
}

// ── Rendering (forward) ────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height, float fps)
{
    return m_impl->m_pipeline->render_scene(scene, camera, width, height, fps);
}

std::shared_ptr<Framebuffer> RenderEngine::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera,
    i32 width, i32 height, float fps)
{
    return m_impl->m_pipeline->render_scene(scene, camera, width, height, fps);
}

std::shared_ptr<Framebuffer> RenderEngine::render(
    const Composition& comp, Frame frame)
{
    // P1-F Pass D — replaces the removed `render_frame()` (which had been
    // `[[deprecated("Use RenderEngine::render()")]]` since Pass A).  Same
    // implementation: delegates into the OPP-side `RenderPipeline` facade
    // (which in turn routes through the SoftwareRenderer+Runtime graph
    // orchestration).  Returns the raw framebuffer for OPP consumers.
    return m_impl->m_pipeline->render_composition(comp, frame);
}

std::shared_ptr<const graph::NodeExecutionError>
RenderEngine::last_render_error() const noexcept {
    return m_impl->m_renderer->session().last_frame_error();
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

// (P1-F Pass D — 2026-06-30)  The OPP-side escape-hatch class
// `chronon3d::advanced::RenderEngineAccess` and the header
// `<chronon3d/advanced/render_engine_access.hpp>` have both been REMOVED
// in lockstep with this body block.  The legacy accessors that lived
// there are now reached only via the canonical public-path
// `RenderEngine::render()` (no more escape hatch to the OPP-side
// SoftwareRenderer).  The `friend` declaration in `RenderEngine::Impl`
// (above) was REMOVED in the same commit.

const Config& RenderEngine::config() const noexcept {
    return m_impl->m_config;
}

void RenderEngine::clear_caches() { m_impl->m_renderer->clear_caches(); }
void RenderEngine::reset_counters() { m_impl->m_renderer->reset_counters(); }

// (P1-F Pass D)  `RenderEngine::create_session()` was REMOVED (was a
// [[deprecated]]-via-advanced path; Pass D closes the escape hatch).

} // namespace chronon3d
