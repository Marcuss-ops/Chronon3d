#pragma once

#include <chronon3d/render_graph/render_backend_stats_api.hpp>

namespace chronon3d::graph::detail {

// Native RenderSurface handles may name FrameTransient images even when the
// backend cannot export them to a video encoder. Such outputs must not enter
// caches or static-bake storage that outlives the frame lease.
[[nodiscard]] inline bool has_frame_scoped_native_surfaces(
    const RenderBackendStatsApi* backend) noexcept {
    return backend && (backend->supports_native_surfaces() ||
                       backend->supports_native_video_surface());
}

} // namespace chronon3d::graph::detail
