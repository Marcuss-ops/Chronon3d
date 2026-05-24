#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/fill.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <vector>

#include "path_cache.hpp"
#include "path_fill.hpp"
#include "path_stroke.hpp"
#include "path_utils.hpp"
#include "pip.hpp"

namespace chronon3d {
namespace renderer {

[[nodiscard]] std::vector<PathContour> flatten_path(const PathShape& path);
[[nodiscard]] raster::BBox compute_path_bbox(const PathShape& path, const Mat4& model, f32 spread = 0.0f);
void draw_path(Framebuffer& fb, const PathShape& path, const Mat4& model, const Color& stroke_color,
               const RenderState* state = nullptr);

} // namespace renderer
} // namespace chronon3d
