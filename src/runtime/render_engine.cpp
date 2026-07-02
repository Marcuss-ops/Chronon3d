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
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/assets/asset_registry.hpp>

// TICKET-118/119 — internal bridge reachable via PUBLIC include from
// `chronon3d_backend_software` (see src/backends/software/CMakeLists.txt).
#include "internal/software_processor_services.hpp"

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
    runtime::RenderRuntime                       m_runtime;
    std::unique_ptr<SoftwareRenderer>            m_renderer;
    // RenderPipeline needs both the renderer (backend) and the runtime
    // (services).  Both are constructed before the pipeline is emplace()d
    // in the body, so std::optional is the right storage here.
    std::optional<runtime::RenderPipeline>       m_pipeline;

    // Fase C2 — unified constructor (replaces the two previous overloads).
    // assets_root is optional; when provided, set_assets_root() is called
    // after the runtime + renderer + backend wiring is complete.
    explicit Impl(Config config, std::optional<std::filesystem::path> assets_root = std::nullopt)
        : m_config(std::move(config))
        , m_runtime(m_config)        // RenderRuntime ctor calls populate()
        , m_renderer(std::make_unique<SoftwareRenderer>(m_runtime, m_config))
    {
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        // TICKET-011 + Fase 1 services-validation — build the
        // SoftwareBackendServices bundle (NO `owner` field — TICKET-118
        // contractive removal) and route through `make_software_backend`
        // so all REQUIRED services are validated at construction.  The
        // orchestrator-only `ProcessorSourceExtras` (registry /
        // image_backend / font_engine) are attached post-construction via
        // `SoftwareBackend::attach_processor_context(...)`.
        chronon3d::SoftwareBackendServices services{
            /* counters           = */ m_renderer->counters(),
            /* settings           = */ &m_renderer->render_settings(),
            /* framebuffer_pool   = */ m_runtime.framebuffer_pool_shared(),
            /* asset_resolver     = */ &m_runtime.resolver(),
            /* text_resources     = */ m_renderer->text_render_resources(),
            /* images             = */ nullptr,
            /* text_raster        = */ nullptr,
            /* debug_config       = */ nullptr,
        };
        auto backend = make_software_backend(services).value();
        attach_processor_context_to_backend_impl(backend.get(), services);
        // Fase C2 — attach_backend() is @deprecated; direct wiring inside Impl
        m_runtime.attach_backend(std::move(backend));

        // TICKET-011a follow-up #1 — publish the RenderPipeline facade.
        m_pipeline.emplace(m_renderer.get(), m_runtime);

        if (assets_root.has_value()) {
            set_assets_root(*assets_root);
            spdlog::debug("RenderEngine::Impl: constructed with assets_root={}",
                          assets_root->string());
        } else {
            spdlog::debug("RenderEngine::Impl: constructed; runtime backend attached");
        }
    }

    // TICKET-118/119 — inline the orchestrator-only processor-context
    // attachment on an already-constructed unique_ptr<SoftwareBackend>.
    // The CALLER retains ownership of `backend` until the subsequent
    // `m_runtime.attach_backend(std::move(backend))` (which transfers
    // ownership to the runtime exactly once).  We only DEREF the raw
    // pointer here to set up `m_proc_ctx`; we never wrap it in a new
    // unique_ptr (that would double-free).  This mirrors the
    // `runtime_adapter.cpp` pattern 1:1 — see that file for the
    // canonical version.
    template <typename SoftwareBackendT>
    void attach_processor_context_to_backend_impl(
        SoftwareBackendT* backend,
        const chronon3d::SoftwareBackendServices& services
    ) {
        chronon3d::backends::software::internal::ProcessorSourceExtras extras{};
        extras.registry      = &m_renderer->software_registry();
        extras.image_backend = m_renderer->image_backend();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
        extras.font_engine   = &m_renderer->font_engine();
#endif
        backend->attach_processor_context(
            chronon3d::backends::software::internal::make_processor_context(
                services, extras));
    }

    ~Impl() = default;

    void set_assets_root(const std::filesystem::path& root) {
        const std::string root_str = root.string();
        m_assets.mount(root);                                         // legacy AssetRegistry mount (for non-resolver consumers)
        m_runtime.resolver().mount(root);                             // WP-8 PR 8.0 sibling resolver, mounted inside the runtime
        // P1 #7 — intentionally call deprecated function as backward-compat mirror
        // for deep code without a runtime in scope.  Suppress the deprecation
        // warning since this is the canonical bridge site.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        runtime::set_process_wide_assets_root(root_str);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
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
    return m_impl->m_runtime.resolver().mount_root().string();
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
