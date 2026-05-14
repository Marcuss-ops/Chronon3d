#pragma once

#include <chronon3d/core/framebuffer.hpp>

// Sub-modules
#include "scanline_rasterizer.hpp"
#include "line_rasterizer.hpp"
#include "shape_rasterizer.hpp"
#include "fake_box3d_renderer.hpp"
#include "grid_plane_renderer.hpp"
#include "mesh_renderer.hpp"
#include "glass_panel_renderer.hpp"
#include "projection_utils.hpp"

namespace chronon3d {
namespace renderer {

// Shared utility
std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h);

} // namespace renderer
} // namespace chronon3d
