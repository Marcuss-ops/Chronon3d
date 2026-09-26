#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>

// Sub-modules
#include "rasterizers/scanline_rasterizer.hpp"
#include "rasterizers/line_rasterizer.hpp"
#include "rasterizers/shape_rasterizer.hpp"
#include "specialized/fake_box3d_renderer.hpp"
#include "specialized/grid_plane_renderer.hpp"
#include "specialized/mesh_renderer.hpp"
#include "utils/projection_utils.hpp"

// downsample_fb() removed — lives in render_graph/pipeline/helpers.hpp (graph namespace).
