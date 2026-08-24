// ===========================================================================
// api/render_engine.cpp — RenderEngine::Impl wired for TICKET-011
//
// Construction sequence inside `Impl`:
//   1) RenderRuntime::create(RuntimeConfig) — populate(): allocates caches,
//                                             pool, executor, plan cache,
//                                             scheduler, 3 registries, and
//                                             3 catalogs (but NOT the backend)
//   2) m_renderer(m_runtime, cfg)           — renderer holds per-instance state
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
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/frame_slot_pipeline.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>  // Fase A2 — attach_software_backend factory
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace chronon3d {

struct PreparedRenderJob::Impl {
    RenderEngine* engine{nullptr};
    std::shared_ptr<const CompiledComposition> compiled;
    Frame count{0};
    bool finished{false};
    runtime::ResourcePlan resource_plan;
    runtime::FrameSlotPipeline<3> slots;
    std::uint64_t next_sequence{0};

    [[nodiscard]] bool can_split_evaluation() const noexcept;
    [[nodiscard]] EvaluatedCompositionFrame evaluate_frame(
        Frame frame, FrameArena& arena) const;
    [[nodiscard]] std::shared_ptr<Framebuffer> render_evaluated_frame(
        EvaluatedCompositionFrame& evaluated, Frame frame) const;
};

PreparedRenderJob::PreparedRenderJob(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

PreparedRenderJob::~PreparedRenderJob() = default;
PreparedRenderJob::PreparedRenderJob(PreparedRenderJob&&) noexcept = default;
PreparedRenderJob& PreparedRenderJob::operator=(PreparedRenderJob&&) noexcept = default;

Frame PreparedRenderJob::frame_count() const noexcept {
    return m_impl ? m_impl->count : Frame{0};
}

const runtime::ResourcePlan& PreparedRenderJob::resource_plan() const noexcept {
    static const runtime::ResourcePlan empty;
    return m_impl ? m_impl->resource_plan : empty;
}

void PreparedRenderJob::finish() noexcept {
    if (!m_impl) return;
    m_impl->finished = true;
    m_impl->compiled.reset();
}

struct RenderEngine::Impl {
    // P1-F Pass D — the friend declaration for
    // `chronon3d::advanced::RenderEngineAccess` was REMOVED here in lockstep
    // with the deletion of `advanced/render_engine_access.hpp` and the
    // corresponding accessor bodies at the bottom of this TU.  OPP-internal
    // access to the SoftwareRenderer is now reached via the public-path
    // `RenderEngine::render()` canonical entry (no more escape hatch).

    Config                                       m_config;
    AssetRegistry                                m_assets;
    // Engine-lifetime authoring registry reused by every evaluated frame.
    registry::ShapeRegistry                      m_shape_registry{
        registry::make_default_shape_registry()};
    std::unique_ptr<runtime::RenderRuntime>    m_runtime;
    std::unique_ptr<SoftwareRenderer>            m_renderer;
    // RenderPipeline needs both the renderer (backend) and the runtime
    // (services).  Both are constructed before the pipeline is emplace()d
    // in the body, so std::optional is the right storage here.
    std::optional<runtime::RenderPipeline>       m_pipeline;

    // Fase C2 / R1 — unified constructor delegates runtime construction to
    // the canonical RenderRuntime::create(RuntimeConfig) factory.
    explicit Impl(Config config, std::optional<std::filesystem::path> assets_root = std::nullopt)
        : m_config(std::move(config))
    {
        auto runtime_result = runtime::RenderRuntime::create(
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
        chronon3d::backends::software::attach_software_backend(
            m_renderer.get(), m_config.backend_preference());

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

#include "render_engine_prepared_job.inc"

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

const CompositionRegistry* RenderEngine::composition_registry() const noexcept {
    if (!m_impl || !m_impl->m_renderer) return nullptr;
    return m_impl->m_renderer->composition_registry();
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
    // Authoring compatibility adapter only: compile once at the boundary,
    // then execute exclusively through the immutable compiled path.
    auto compiled = chronon3d::compile_composition(
        comp, CompositionCompileContext{});
    if (!compiled) {
        throw std::runtime_error(
            "Composition compilation failed for '" + comp.name() +
            "': " + compiled.error().message);
    }
    return render_compiled(std::move(compiled).value(), frame);
}

std::shared_ptr<Framebuffer> RenderEngine::render_compiled(
    const CompiledComposition& compiled, Frame frame)
{
    if (!compiled.definition) {
        throw std::runtime_error(
            "CompiledComposition has no definition");
    }
    if (compiled.asset_manifest) {
        const auto integrity = assets::verify_asset_manifest(
            *compiled.asset_manifest, m_impl->m_runtime->resolver());
        if (!integrity) {
            throw std::runtime_error(
                "Prepared asset integrity check failed for '" +
                integrity.error().logical_path + "': " +
                integrity.error().message);
        }
    }
    auto settings = m_impl->m_renderer->render_settings();
    settings.render_budget = compiled.render_budget;
    m_impl->m_renderer->set_settings(settings);
    m_impl->m_renderer->session().clear_last_frame_error();
    return m_impl->m_pipeline->render_compiled_composition(compiled, frame);
}

PreparedRenderJob RenderEngine::prepare(
    const Composition& comp,
    const PreparedRenderJobOptions& options) {
    if (options.pipeline_depth != runtime::FrameSlotPipeline<3>::depth()) {
        throw std::invalid_argument(
            "PreparedRenderJob currently requires a pipeline depth of exactly 3");
    }
    if (options.node_cache_capacity_bytes.has_value()) {
        m_impl->m_renderer->node_cache().set_capacity(
            *options.node_cache_capacity_bytes);
    }
    auto compiled_result = chronon3d::compile_composition(
        comp, CompositionCompileContext{});
    if (!compiled_result) {
        throw std::runtime_error(
            "Composition compilation failed for '" + comp.name() +
            "': " + compiled_result.error().message);
    }

    auto compiled = std::make_shared<const CompiledComposition>(
        std::move(compiled_result).value());
    // Preparation is also the graph barrier for a batch job.  The first
    // dummy render builds the canonical graph and its existing physical
    // lifetime/aliasing plan; the second pass exercises the warm cache.  The
    // actual frame loop therefore enters with assets, pools, graph topology,
    // and transient-slot planning already primed.
    runtime::RenderPreparationOptions preparation_options;
    preparation_options.warmup_renderer = true;
    preparation_options.warmup.render_dummy_frame = true;
    preparation_options.warmup.dummy_frame = Frame{0};
    preparation_options.warmup.width = comp.width();
    preparation_options.warmup.height = comp.height();
    const auto preparation = runtime::prepare_render(
        m_impl->m_renderer.get(), *compiled, preparation_options);
    if (!preparation.ok()) {
        throw std::runtime_error(
            "Render job preparation failed: " + preparation.diagnostic());
    }

    // Reserve the renderer-owned persistent frame resources before the job
    // becomes executable. The graph warm-up covers the pool-backed outputs;
    // these two session resources otherwise grow lazily from execute().
    auto& framebuffer_pool = m_impl->m_renderer->software_framebuffer_pool();
    m_impl->m_renderer->buffer_ring().ensure_size(
        comp.width(), comp.height(), &framebuffer_pool);
    (void)m_impl->m_renderer->scratch_buffer().ensure_size(
        comp.width(), comp.height());
    m_impl->m_renderer->software_session().software.dof_scratch.ensure_size(
        0, 0, 0, 0);
    m_impl->m_renderer->software_session().software.effect_scratch.ensure_size(
        comp.width(), comp.height());

    auto impl = std::make_unique<PreparedRenderJob::Impl>();
    impl->engine = this;
    impl->compiled = std::move(compiled);
    impl->count = impl->compiled->definition
        ? impl->compiled->definition->composition.duration
        : Frame{0};
    runtime::ResourcePlanner planner;
    if (const auto* graph = m_impl->m_renderer->graph_cache().peek(
            comp.width(), comp.height()); graph != nullptr) {
        const auto& physical = graph->physical_framebuffer_plan;
        for (std::size_t id = 0; id < physical.resources.size(); ++id) {
            const auto& allocation = physical.resources[id];
            if (allocation.producer == graph::k_invalid_node ||
                id >= graph->lifetimes.size()) {
                continue;
            }
            const auto& lifetime = graph->lifetimes[id];
            const bool persistent = allocation.persistent || allocation.async_use;
            planner.add(runtime::ResourceRequest{
                .id = std::string{"GraphNode["} + std::to_string(id) + "]",
                .kind = runtime::ResourceKind::Color,
                .bytes = static_cast<std::size_t>(comp.width()) *
                    static_cast<std::size_t>(comp.height()) * sizeof(Color),
                .lifetime = persistent
                    ? runtime::LifetimeClass::JobPersistent
                    : runtime::LifetimeClass::FrameTransient,
                .first = persistent ? 0 : lifetime.first_level,
                .last = persistent ? 0 : lifetime.last_level,
                .alignment = alignof(Color)});
        }
    }
    const auto rgba_bytes = static_cast<std::size_t>(comp.width()) *
        static_cast<std::size_t>(comp.height()) * 4u * sizeof(float);
    for (std::size_t slot = 0; slot < options.pipeline_depth; ++slot) {
        const auto id = std::string{"FrameSlot["} + std::to_string(slot) + "].rgba";
        planner.add(runtime::ResourceRequest{
            .id = id,
            .kind = runtime::ResourceKind::Color,
            .bytes = rgba_bytes,
            .lifetime = runtime::LifetimeClass::PipelineSlot,
            .first = 0,
            .last = options.pipeline_depth - 1,
            .alignment = 64});
    }
    impl->resource_plan = planner.build();
    return PreparedRenderJob(std::move(impl));
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
