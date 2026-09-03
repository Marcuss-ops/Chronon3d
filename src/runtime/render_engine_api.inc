// RenderEngine public API facade. Included by render_engine.cpp after the
// private implementation types are complete so the PImpl boundary stays local
// to a single translation unit.

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

// ── Rendering ──────────────────────────────────────────────────────────────

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
    if (!compiled.composition) {
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
    if (options.pipeline_depth != 3) {
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
    impl->count = impl->compiled->composition
        ? impl->compiled->composition->duration()
        : Frame{0};
    runtime::ResourcePlanner planner;
    if (const auto* graph = m_impl->m_renderer->graph_cache().peek(
            comp.width(), comp.height()); graph != nullptr) {
        const auto& physical = graph->resource_table();
        for (std::size_t id = 0; id < physical.resources.size(); ++id) {
            const auto& allocation = physical.resources[id];
            if (allocation.producer == graph::k_invalid_node) {
                continue;
            }
            const bool persistent = allocation.persistent || allocation.async_use;
            planner.add(runtime::ResourceRequest{
                std::string{"GraphNode["} + std::to_string(id) + "]",
                runtime::ResourceKind::Color,
                static_cast<std::size_t>(comp.width()) *
                    static_cast<std::size_t>(comp.height()) * sizeof(Color),
                persistent
                    ? runtime::LifetimeClass::JobPersistent
                    : runtime::LifetimeClass::FrameTransient,
                persistent ? std::size_t{0} : allocation.first_level,
                persistent ? std::size_t{0} : allocation.last_level,
                alignof(Color),
                runtime::ResourceDesc{}});
        }
    }
    const auto rgba_bytes = static_cast<std::size_t>(comp.width()) *
        static_cast<std::size_t>(comp.height()) * 4u * sizeof(float);
    for (std::size_t slot = 0; slot < options.pipeline_depth; ++slot) {
        const auto id = std::string{"FrameSlot["} + std::to_string(slot) + "].rgba";
        planner.add(runtime::ResourceRequest{
            id,
            runtime::ResourceKind::Color,
            rgba_bytes,
            runtime::LifetimeClass::PipelineSlot,
            std::size_t{0},
            options.pipeline_depth - 1,
            std::size_t{64},
            runtime::ResourceDesc{}});
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

// ── Settings / accessors ──────────────────────────────────────────────────

void RenderEngine::set_settings(const RenderSettings& settings) {
    m_impl->m_renderer->set_settings(settings);
}

const RenderSettings& RenderEngine::settings() const noexcept {
    return m_impl->m_renderer->render_settings();
}

const Config& RenderEngine::config() const noexcept {
    return m_impl->m_config;
}

void RenderEngine::clear_caches() { m_impl->m_renderer->clear_caches(); }
void RenderEngine::reset_counters() { m_impl->m_renderer->reset_counters(); }
