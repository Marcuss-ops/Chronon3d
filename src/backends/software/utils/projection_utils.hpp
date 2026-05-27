#pragma once

#include <chronon3d/math/math_base.hpp>
#include <chronon3d/math/math_base.hpp>
#include <chronon3d/math/math_base.hpp>

namespace chronon3d {
namespace renderer {

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok);

} // namespace renderer
} // namespace chronon3d
