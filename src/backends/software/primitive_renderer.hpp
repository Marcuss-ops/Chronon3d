#pragma once

#include <chronon3d/core/framebuffer.hpp>

// Sub-modules
#include "rasterizers/scanline_rasterizer.hpp"
#include "rasterizers/line_rasterizer.hpp"
#include "rasterizers/shape_rasterizer.hpp"
#include "specialized/fake_box3d_renderer.hpp"
#include "specialized/grid_plane_renderer.hpp"
#include "specialized/mesh_renderer.hpp"
#include "specialized/glass_panel_renderer.hpp"
#include "utils/projection_utils.hpp"

namespace chronon3d {
namespace renderer {

// Shared utility
std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h);

} // namespace renderer
} // namespace chronon3d
