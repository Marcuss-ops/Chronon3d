#pragma once

#include <chronon3d/render_graph/render_backend_stats_api.hpp>
#include <chronon3d/render_graph/render_backend_frame_api.hpp>
#include <chronon3d/render_graph/render_backend_surface_api.hpp>
#include <chronon3d/render_graph/render_backend_texture_api.hpp>

namespace chronon3d::graph {

// The combined facade retains the canonical backend type while its contracts
// can also be consumed independently by domain-specific code.
class RenderBackend : public RenderBackendStatsApi,
                      public RenderBackendFrameApi,
                      public RenderBackendSurfaceApi,
                      public RenderBackendTextureApi {
public:
    RenderBackend() = default;
    ~RenderBackend() override = default;
    RenderBackend(const RenderBackend&) = delete;
    RenderBackend& operator=(const RenderBackend&) = delete;
    RenderBackend(RenderBackend&&) noexcept = default;
    RenderBackend& operator=(RenderBackend&&) noexcept = default;
};

} // namespace chronon3d::graph
