#include "line_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color) {
    if (color.a <= 0.0f) return;
    i32 x0 = static_cast<i32>(p0.x), y0 = static_cast<i32>(p0.y);
    i32 x1 = static_cast<i32>(p1.x), y1 = static_cast<i32>(p1.y);
    const i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy, e2;
    while (true) {
        if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height())
            fb.set_pixel(x0, y0, compositor::blend(color, fb.get_pixel(x0, y0), BlendMode::Normal));
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

bool clip_and_project_line(const Vec3& w0, const Vec3& w1,
                           const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy,
                           Vec2& p0, Vec2& p1) {
    Vec4 c0 = view * Vec4(w0, 1.0f);
    Vec4 c1 = view * Vec4(w1, 1.0f);
    constexpr f32 near = 0.5f;

    if (c0.z >= -near && c1.z >= -near) return false;

    if (c0.z >= -near) {
        const f32 t = (-near - c1.z) / (c0.z - c1.z);
        c0 = c1 + (c0 - c1) * t;
    }
    if (c1.z >= -near) {
        const f32 t = (-near - c0.z) / (c1.z - c0.z);
        c1 = c0 + (c1 - c0) * t;
    }

    const f32 ps0 = focal / (-c0.z);
    const f32 ps1 = focal / (-c1.z);
    p0 = {c0.x * ps0 + vp_cx, -c0.y * ps0 + vp_cy};
    p1 = {c1.x * ps1 + vp_cx, -c1.y * ps1 + vp_cy};
    return true;
}

} // namespace renderer
} // namespace chronon3d
