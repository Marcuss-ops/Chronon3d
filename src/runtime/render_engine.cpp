// ===========================================================================
// api/render_engine.cpp — RenderEngine private assembly and implementation
// boundaries. Prepared-job execution and public API methods live in dedicated
// implementation fragments included below, preserving one translation unit.
// ===========================================================================

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/frame_slot_payload.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <array>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace chronon3d {

struct PreparedRenderJob::Impl {
    RenderEngine* engine{nullptr};
    bool finished{false};

    // JobSession is the sole owner of job-lifetime queues, slots, payloads,
    // cancellation and counters. RenderPipeline owns stage orchestration;
    // PreparedRenderJob only retains the API handle state.
    std::unique_ptr<runtime::JobSession> job_session;
    runtime::RenderPipeline* pipeline{nullptr};
    // Compatibility-only owner for arbitrary Composition callbacks. Typed
    // jobs keep their immutable CompiledProgram in JobSession instead.
    std::shared_ptr<const CompiledComposition> compatibility_composition;
};

PreparedRenderJob::PreparedRenderJob(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

PreparedRenderJob::~PreparedRenderJob() = default;
PreparedRenderJob::PreparedRenderJob(PreparedRenderJob&&) noexcept = default;
PreparedRenderJob& PreparedRenderJob::operator=(PreparedRenderJob&&) noexcept = default;

Frame PreparedRenderJob::frame_count() const noexcept {
    if (!m_impl || !m_impl->job_session) return Frame{0};
    if (const auto& program = m_impl->job_session->program_ptr()) {
        return Frame{program->metadata().duration_frames};
    }
    return m_impl->compatibility_composition &&
        m_impl->compatibility_composition->composition
        ? m_impl->compatibility_composition->composition->duration() : Frame{0};
}

const runtime::ResourcePlan& PreparedRenderJob::resource_plan() const noexcept {
    static const runtime::ResourcePlan empty;
    return m_impl && m_impl->job_session
        ? m_impl->job_session->resource_plan() : empty;
}

void PreparedRenderJob::finish() noexcept {
    if (!m_impl) return;
    m_impl->finished = true;
    if (m_impl->job_session) m_impl->job_session->reset_job();
}

struct RenderEngine::Impl {
    Config                                    m_config;
    AssetRegistry                             m_assets;
    registry::ShapeRegistry                   m_shape_registry{
        registry::make_default_shape_registry()};
    std::unique_ptr<runtime::RuntimeContext>    m_runtime;
    std::unique_ptr<SoftwareRenderer>          m_renderer;
    std::optional<runtime::RenderPipeline>     m_pipeline;

    explicit Impl(Config config, std::optional<std::filesystem::path> assets_root = std::nullopt)
        : m_config(std::move(config))
    {
        auto runtime_result = runtime::RuntimeContext::create(
            runtime::RuntimeConfig{.config = m_config, .assets_root = assets_root});
        if (!runtime_result) {
            throw std::runtime_error(
                "RenderEngine::Impl: runtime assembly failed: " +
                runtime_result.error().message);
        }
        m_runtime = std::move(runtime_result).value();
        m_renderer = std::make_unique<SoftwareRenderer>(*m_runtime, m_config);
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        chronon3d::backends::software::attach_software_backend(
            m_renderer.get(), m_config.backend_preference());
        m_pipeline.emplace(m_renderer.get(), m_renderer->execution_context());

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
        m_runtime->resolver().mount(root);
    }
};

#include "render_engine_prepared_job_detail.hpp"
#include "render_engine_api_detail.hpp"

} // namespace chronon3d
