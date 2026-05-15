#include "scanline_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>

namespace chronon3d {
namespace renderer {

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color) {
    if (color.a <= 0.0f) return;

    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 4; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }

    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = v[i], & b = v[(i + 1) % 4];
            if ((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy)) {
                const f32 t = (fy - a.y) / (b.y - a.y);
                const f32 x = a.x + t * (b.x - a.x);
                x_left  = std::min(x_left, x);
                x_right = std::max(x_right, x);
            }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width() - 1, static_cast<i32>(std::floor(x_right)));
        for (i32 x = x0; x <= x1; ++x)
            fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
    }
}

void fill_gradient_quad(Framebuffer& fb, const Vec2 v[4], const Color colors[4]) {
    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 4; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }
    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        Color cl{}, cr{};
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = v[i], &b = v[(i+1)%4];
            if (!((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy))) continue;
            const f32 t = (fy - a.y) / (b.y - a.y);
            const f32 x = a.x + t*(b.x - a.x);
            const Color& ca = colors[i]; const Color& cb = colors[(i+1)%4];
            Color c{ca.r+t*(cb.r-ca.r), ca.g+t*(cb.g-ca.g),
                    ca.b+t*(cb.b-ca.b), ca.a+t*(cb.a-ca.a)};
            if (x < x_left)  { x_left  = x; cl = c; }
            if (x > x_right) { x_right = x; cr = c; }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width()-1, static_cast<i32>(std::floor(x_right)));
        const f32 span = x_right - x_left;
        for (i32 x = x0; x <= x1; ++x) {
            f32 t = (span > 0.01f) ? (x - x_left) / span : 0.5f;
            Color c{cl.r+t*(cr.r-cl.r), cl.g+t*(cr.g-cl.g),
                    cl.b+t*(cr.b-cl.b), cl.a+t*(cr.a-cl.a)};
            if (c.a > 0.01f)
                fb.set_pixel(x, y, compositor::blend(c, fb.get_pixel(x, y), BlendMode::Normal));
        }
    }
}

void fill_triangle(Framebuffer& fb, const Vec2 v[3], const Color& color) {
    if (color.a <= 0.0f) return;

    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 3; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }

    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        for (int i = 0; i < 3; ++i) {
            const Vec2& a = v[i], & b = v[(i + 1) % 3];
            if ((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy)) {
                const f32 t = (fy - a.y) / (b.y - a.y);
                const f32 x = a.x + t * (b.x - a.x);
                x_left  = std::min(x_left, x);
                x_right = std::max(x_right, x);
            }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width() - 1, static_cast<i32>(std::floor(x_right)));
        for (i32 x = x0; x <= x1; ++x)
            fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
    }
}

void fill_gradient_triangle(Framebuffer& fb, const Vec2 v[3], const Color colors[3]) {
    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 3; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }
    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        Color cl{}, cr{};
        for (int i = 0; i < 3; ++i) {
            const Vec2& a = v[i], &b = v[(i+1)%3];
            if (!((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy))) continue;
            const f32 t = (fy - a.y) / (b.y - a.y);
            const f32 x = a.x + t*(b.x - a.x);
            const Color& ca = colors[i]; const Color& cb = colors[(i+1)%3];
            Color c{ca.r+t*(cb.r-ca.r), ca.g+t*(cb.g-ca.g),
                    ca.b+t*(cb.b-ca.b), ca.a+t*(cb.a-ca.a)};
            if (x < x_left)  { x_left  = x; cl = c; }
            if (x > x_right) { x_right = x; cr = c; }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width()-1, static_cast<i32>(std::floor(x_right)));
        const f32 span = x_right - x_left;
        for (i32 x = x0; x <= x1; ++x) {
            f32 t = (span > 0.01f) ? (x - x_left) / span : 0.5f;
            Color c{cl.r+t*(cr.r-cl.r), cl.g+t*(cr.g-cl.g),
                    cl.b+t*(cr.b-cl.b), cl.a+t*(cr.a-cl.a)};
            if (c.a > 0.01f)
                fb.set_pixel(x, y, compositor::blend(c, fb.get_pixel(x, y), BlendMode::Normal));
        }
    }
}

} // namespace renderer
} // namespace chronon3d
