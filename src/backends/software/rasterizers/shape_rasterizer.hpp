#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/fill.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {
namespace renderer {

// fill=nullptr → uses solid color only.
void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread = 0.0f, const RenderState* state = nullptr,
                             const Fill* fill = nullptr);

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
bool hit_test(const Shape& s, Vec2 p, f32 spread = 0.0f);
bool pixel_passes_mask(const RenderState& state, i32 x, i32 y);

} // namespace renderer
} // namespace chronon3d
