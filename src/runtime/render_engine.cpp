// ===========================================================================
// api/render_engine.cpp — RenderEngine private assembly and implementation
// boundaries. Prepared-job execution and public API methods live in dedicated
// implementation fragments included below, preserving one translation unit.
// ===========================================================================

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/frame_execution_slot_ring.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>
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
    struct SlotPayload {
        FrameArena arena{4u * 1024u * 1024u, true};
        std::optional<EvaluatedCompositionFrame> evaluated{};
        std::shared_ptr<Framebuffer> rendered{};

        void reset() noexcept {
            evaluated.reset();
            rendered.reset();
            arena.reset();
        }
    };

    RenderEngine* engine{nullptr};
    std::shared_ptr<const CompiledComposition> compiled;
    Frame count{0};
    bool finished{false};
    runtime::ResourcePlan resource_plan;
    runtime::FrameExecutionSlotRing slots{3};
    std::array<SlotPayload, 3> payloads{};
    std::uint64_t next_sequence{0};

    [[nodiscard]] SlotPayload& payload(const runtime::FrameExecutionSlot& slot) noexcept {
        return payloads[slot.slot_id];
    }

    void reset_slots() noexcept {
        for (auto& payload : payloads) payload.reset();
        slots.reset();
    }

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
    Config                                    m_config;
    AssetRegistry                             m_assets;
    registry::ShapeRegistry                   m_shape_registry{
        registry::make_default_shape_registry()};
    std::unique_ptr<runtime::RenderRuntime>    m_runtime;
    std::unique_ptr<SoftwareRenderer>          m_renderer;
    std::optional<runtime::RenderPipeline>     m_pipeline;

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

        chronon3d::backends::software::attach_software_backend(
            m_renderer.get(), m_config.backend_preference());
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
        m_runtime->resolver().mount(root);
    }
};

#include "render_engine_prepared_job.inc"
#include "render_engine_api.inc"

} // namespace chronon3d
