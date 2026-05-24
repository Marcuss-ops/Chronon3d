#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/fill.hpp>

namespace chronon3d::renderer {

Color resolve_fill_color(const Fill& fill, Vec2 p, const raster::BBox& bbox, f32 opacity);

} // namespace chronon3d::renderer
