#include "cinematic_motion_scenes.hpp"

#include <algorithm>
#include <cmath>

namespace chronon3d::test {

namespace {

void draw_line(Framebuffer& fb, int x0, int y0, int x1, int y1, Color color) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        fb.set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace

Framebuffer& blit_into(Framebuffer& dest, const Framebuffer& src, int dst_x0, int dst_y0) {
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            dest.set_pixel(dst_x0 + x, dst_y0 + y, src.get_pixel(x, y));
        }
    }
    return dest;
}

Framebuffer composite_contact_sheet_3x3(
    const std::vector<std::shared_ptr<Framebuffer>>& panels) {
    constexpr int cell_w = 320;
    constexpr int cell_h = 180;
    Framebuffer result(cell_w * 3, cell_h * 3);
    result.clear(Color::transparent());

    for (int i = 0; i < 9; ++i) {
        if (!panels.empty() && i < static_cast<int>(panels.size()) && panels[i]) {
            const int x = (i % 3) * cell_w;
            const int y = (i / 3) * cell_h;
            blit_into(result, *panels[i], x, y);
        }
    }
    return result;
}

Framebuffer composite_quad_2x2(
    const std::vector<std::shared_ptr<Framebuffer>>& quads) {
    constexpr int cell_w = 480;
    constexpr int cell_h = 270;
    Framebuffer result(cell_w * 2, cell_h * 2);
    result.clear(Color::transparent());

    for (int i = 0; i < 4; ++i) {
        if (!quads.empty() && i < static_cast<int>(quads.size()) && quads[i]) {
            const int x = (i % 2) * cell_w;
            const int y = (i / 2) * cell_h;
            blit_into(result, *quads[i], x, y);
        }
    }
    return result;
}

void draw_crosshair(Framebuffer& fb, int cx, int cy, Color color, int arm_len) {
    draw_line(fb, cx - arm_len, cy, cx + arm_len, cy, color);
    draw_line(fb, cx, cy - arm_len, cx, cy + arm_len, color);
}

void draw_axis_tripod(Framebuffer& fb, int cx, int cy,
                      Vec3 x_dir, Vec3 y_dir, Vec3 z_dir,
                      float scale) {
    draw_line(fb, cx, cy, cx + static_cast<int>(std::round(x_dir.x * scale)),
              cy - static_cast<int>(std::round(x_dir.y * scale)), Color::red());
    draw_line(fb, cx, cy, cx + static_cast<int>(std::round(y_dir.x * scale)),
              cy - static_cast<int>(std::round(y_dir.y * scale)), Color::green());
    draw_line(fb, cx, cy, cx + static_cast<int>(std::round(z_dir.x * scale)),
              cy - static_cast<int>(std::round(z_dir.y * scale)), Color::blue());
}

} // namespace chronon3d::test
