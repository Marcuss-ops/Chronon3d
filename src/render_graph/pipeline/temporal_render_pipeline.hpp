#pragma once

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "temporal_render_context.hpp"

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_scene_via_graph_temporal(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    float fps,
    std::string_view diagnostic_label,
    chronon3d::SoftwareRenderer* sw_sidecar,
    const TemporalRenderContext* temporal_context);

std::shared_ptr<Framebuffer> render_compiled_composition_frame_temporal(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const CompiledComposition& compiled,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar,
    chronon3d::CancellationToken* cancellation);

} // namespace chronon3d::graph
