#include "projection_utils.hpp"

namespace chronon3d {
namespace renderer {

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok) {
    Vec4 cam = view * Vec4(wp, 1.0f);
    if (cam.z >= 0.0f) { ok = false; return {}; }
    const f32 ps = focal / (-cam.z);
    ok = true;
    return {cam.x * ps + vp_cx, -cam.y * ps + vp_cy};
}

} // namespace renderer
} // namespace chronon3d
