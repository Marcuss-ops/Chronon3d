// RenderEngine public API facade. Included by render_engine.cpp after the
// private implementation types are complete so the PImpl boundary stays local
// to a single translation unit.

#include <chronon3d/cache/cache_diagnostics.hpp>

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

std::shared_ptr<Framebuffer> RenderEngine::render_program(
    const compiler::CompiledProgram& program, Frame frame)
{
    return render_program(program, frame, nullptr, 0);
}

std::shared_ptr<Framebuffer> RenderEngine::render_program(
    const compiler::CompiledProgram& program, Frame frame,
    const chronon_layer_state* layer_states, std::size_t layer_state_count)
{
    return render_program(program, frame, layer_states, layer_state_count, nullptr, 0);
}

std::shared_ptr<Framebuffer> RenderEngine::render_program(
    const compiler::CompiledProgram& program, Frame frame,
    const chronon_layer_state* layer_states, std::size_t layer_state_count,
    const chronon_element_state* element_states, std::size_t element_state_count)
{
    // ADR-032: refuse a malformed handshake here, at the execution boundary,
    // before any render work starts — the same place and the same reason the
    // asset integrity check below runs.
    if (motion::ContractApplyReport refused{};
        !motion::validate_layer_states(layer_states, layer_state_count, &refused)) {
        throw std::runtime_error(
            "ADR-032 motion handshake refused " + std::to_string(refused.rejected) +
            " layer state record(s): struct_size, reserved (0) or out-of-range "
            "layer id, duplicate layer id, or non-finite matrix");
    }
    // CompiledProgram carries the immutable preflight manifest. Verify it at
    // the execution boundary before
    // decoders reopen source files, closing the same TOCTOU window for the
    // canonical compiler path used by the C ABI.
    if (!program.metadata().asset_manifest.assets().empty()) {
        const auto integrity = assets::verify_asset_manifest(
            program.metadata().asset_manifest,
            m_impl->m_runtime->resolver());
        if (!integrity) {
            throw std::runtime_error(
                "Prepared asset integrity check failed for '" +
                integrity.error().logical_path + "': " +
                integrity.error().message);
        }
    }
    // Apply the program's render budget before execution.
    auto settings = m_impl->m_renderer->render_settings();
    const auto& b = program.metadata().render_budget;
    settings.render_budget.max_width = b.max_width;
    settings.render_budget.max_height = b.max_height;
    settings.render_budget.max_total_pixels = b.max_total_pixels;
    settings.render_budget.max_frames = b.max_frames;
    settings.render_budget.max_layers = b.max_layers;
    settings.render_budget.max_text_bytes = b.max_text_bytes;
    settings.render_budget.max_asset_reference_bytes = b.max_asset_reference_bytes;
    settings.render_budget.max_estimated_output_bytes = b.max_estimated_output_bytes;
    settings.render_budget.max_peak_memory_bytes = b.max_peak_memory_bytes;
    settings.render_budget.max_temporal_pixels = b.max_temporal_pixels;
    m_impl->m_renderer->set_settings(settings);
    m_impl->m_renderer->session().clear_last_frame_error();
    if (motion::ElementApplyReport element_refused{};
        !motion::validate_element_states(element_states, element_state_count, &element_refused)) {
        throw std::runtime_error(
            "element state handshake refused " + std::to_string(element_refused.rejected) +
            " record(s)");
    }
    return m_impl->m_pipeline->render_program(
        program, frame, nullptr, layer_states, layer_state_count,
        element_states, element_state_count);
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

std::string RenderEngine::resolved_lane_name() const {
    return m_impl ? m_impl->resolved_lane_name() : "software";
}

std::string RenderEngine::cache_snapshot() const {
    if (!m_impl || !m_impl->m_runtime) return {};
    return cache::format_cache_snapshot(m_impl->m_runtime->diagnostics());
}
