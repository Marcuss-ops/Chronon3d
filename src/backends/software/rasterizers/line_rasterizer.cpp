#include "line_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color, f32 thickness, const std::optional<raster::BBox>& clip) {
    if (color.a <= 0.0f) return;


    if (thickness <= 0.0f) return;

    // Thick line rasterizer using anti-aliased capsule distance field
    const f32 radius = thickness * 0.5f;
    const f32 pad = radius + 1.0f;

    f32 min_x = std::min(p0.x, p1.x) - pad;
    f32 max_x = std::max(p0.x, p1.x) + pad;
    f32 min_y = std::min(p0.y, p1.y) - pad;
    f32 max_y = std::max(p0.y, p1.y) + pad;

    i32 x0 = std::clamp(static_cast<i32>(std::floor(min_x)), 0, fb.width());
    i32 x1 = std::clamp(static_cast<i32>(std::ceil(max_x)), 0, fb.width());
    i32 y0 = std::clamp(static_cast<i32>(std::floor(min_y)), 0, fb.height());
    i32 y1 = std::clamp(static_cast<i32>(std::ceil(max_y)), 0, fb.height());

    if (clip) {
        x0 = std::max(x0, clip->x0);
        x1 = std::min(x1, clip->x1);
        y0 = std::max(y0, clip->y0);
        y1 = std::min(y1, clip->y1);
    }

    if (x0 >= x1 || y0 >= y1) return;

    const Vec2 ab = p1 - p0;
    const f32 len_sq = glm::dot(ab, ab);

    for (i32 y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x < x1; ++x) {
            const Vec2 p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
            f32 dist = 0.0f;
            if (len_sq < 1e-6f) {
                dist = glm::length(p - p0);
            } else {
                const f32 t = std::clamp(glm::dot(p - p0, ab) / len_sq, 0.0f, 1.0f);
                const Vec2 closest = p0 + ab * t;
                dist = glm::length(p - closest);
            }

            const f32 aa = 1.0f;
            f32 cov = 0.0f;
            if (dist <= radius - aa) {
                cov = 1.0f;
            } else if (dist < radius + aa) {
                cov = 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
            }

            if (cov > 0.0f) {
                Color c = color;
                c.a *= cov;
                row[x] = compositor::blend(c, row[x], BlendMode::Normal);
            }
        }
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
