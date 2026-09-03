#pragma once

#include <chronon3d/media/render_to_media.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d::graph {

/// Attach the media boundary to an already compiled graph. The resolver only
/// derives conversion/zero-copy policy; all resource lifetime, allocation and
/// synchronization remain owned by CompiledResourceTable/ResourceTransition.
[[nodiscard]] inline bool compile_render_to_media(
    CompiledFrameGraph& graph,
    GraphNodeId source_resource,
    GraphNodeId destination_resource,
    const media::MediaSurfaceDesc& destination,
    media::ZeroCopyPolicy policy,
    bool backend_supports_native_video_surface) noexcept {
    auto plan = media::RenderToMediaResolver::resolve(
        graph.resource_table(), source_resource, destination_resource,
        destination, policy, backend_supports_native_video_surface);
    if (!plan) {
        graph.render_to_media.reset();
        return false;
    }
    graph.render_to_media = *plan;
    return true;
}

} // namespace chronon3d::graph
