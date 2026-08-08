#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>

namespace chronon3d::graph {

/// CLI-only bridge. The implementation lives in the internal render-graph
/// pipeline and is intentionally not part of the installed SDK headers.
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
