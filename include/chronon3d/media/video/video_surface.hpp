#pragma once

#include <chronon3d/runtime/render_surface_handle.hpp>

#include <cstdint>

namespace chronon3d::media::video {

struct VideoSurface {
    runtime::RenderSurfaceHandle handle{runtime::kInvalidRenderSurfaceHandle};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

} // namespace chronon3d::media::video
