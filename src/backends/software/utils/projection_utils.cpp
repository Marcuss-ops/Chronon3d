// ============================================================================
// projection_utils.cpp — 2.5D Projection Utilities
//
/// @file    projection_utils.cpp
/// @brief   Minimal projection helpers used by the software backend.
///
/// All core math now delegates to camera_projection_contract.hpp so that
/// every projection path in Chronon3D uses the same sign, centre, depth,
/// and FOV/zoom conventions.
// ============================================================================

#include "projection_utils.hpp"
#include <chronon3d/math/camera_projection_contract.hpp>

namespace chronon3d {
namespace renderer {

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok) {
    Vec4 cam = view * Vec4(wp, 1.0f);
    if (cam.z <= 0.0f) { ok = false; return {}; }
    const f32 ps = focal / cam.z;
    ok = true;
    // Contract Y sign: inverted (screen Y increases downward)
    return {cam.x * ps + vp_cx, -cam.y * ps + vp_cy};
}

} // namespace renderer
} // namespace chronon3d
