#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>

#include "composition_frame_internal.hpp"

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_compiled_composition_frame_temporal(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const CompiledComposition& compiled,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar,
    chronon3d::CancellationToken* cancellation,
    chronon3d::runtime::FrameExecution* execution) {
    detail::CompositionFrameState state(
        backend, node_cache, settings, registry, video_decoder, compiled,
        frame, sw_sidecar, cancellation, execution);
    const bool rendered = state.temporal_accumulation
        ? state.render_temporal_accumulation()
        : state.render_single_frame();
    if (!rendered) {
        return nullptr;
    }
    return state.finalize();
}

std::shared_ptr<Framebuffer> render_compiled_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const CompiledComposition& compiled,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar,
    chronon3d::runtime::FrameExecution* execution) {
    return render_compiled_composition_frame_temporal(
        backend, node_cache, settings, registry, video_decoder, compiled,
        frame, sw_sidecar, nullptr, execution);
}

}  // namespace chronon3d::graph
