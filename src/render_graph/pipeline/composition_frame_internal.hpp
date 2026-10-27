#pragma once

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "temporal/temporal_render_pipeline.hpp"

#include <memory>
#include <string>

#include "temporal/temporal_render_context.hpp"

namespace chronon3d::graph::detail {

struct CompositionFrameState {
    RenderBackend& backend;
    cache::NodeCache& node_cache;
    const RenderSettings& settings;
    const CompositionRegistry* registry;
    media::MediaFrameProvider* video_decoder;
    const CompiledComposition& compiled;
    Frame frame;
    SoftwareRenderer* sw_sidecar;
    CancellationToken* cancellation;
    runtime::FrameExecution* execution;
    runtime::RuntimeContext* frame_runtime;
    const CompositionSpec& spec;
    std::string frame_assets_root;
    float ssaa;
    int w;
    int h;
    int rw;
    int rh;
    bool temporal_accumulation;
    int layer_count{0};
    std::shared_ptr<Framebuffer> render_fb;

    CompositionFrameState(
        RenderBackend& backend,
        cache::NodeCache& node_cache,
        const RenderSettings& settings,
        const CompositionRegistry* registry,
        media::MediaFrameProvider* video_decoder,
        const CompiledComposition& compiled,
        Frame frame,
        SoftwareRenderer* sw_sidecar,
        CancellationToken* cancellation,
        runtime::FrameExecution* execution);

    [[nodiscard]] Scene evaluate_scene(const FrameContext& context) const;
    [[nodiscard]] std::shared_ptr<Framebuffer> call_graph(
        const Scene& scene,
        Frame frame,
        f32 time,
        const TemporalRenderContext* temporal_context = nullptr,
        const RenderSettings* graph_settings = nullptr) const;
    [[nodiscard]] bool render_single_frame();
    [[nodiscard]] bool render_temporal_accumulation();
    [[nodiscard]] std::shared_ptr<Framebuffer> finalize();
};

}  // namespace chronon3d::graph::detail
