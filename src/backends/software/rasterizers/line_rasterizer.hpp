#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {
namespace renderer {

void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color, f32 thickness = 1.0f, const std::optional<raster::BBox>& clip = std::nullopt);

bool clip_and_project_line(const Vec3& w0, const Vec3& w1,
                           const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy,
                           Vec2& p0, Vec2& p1);

} // namespace renderer
} // namespace chronon3d
