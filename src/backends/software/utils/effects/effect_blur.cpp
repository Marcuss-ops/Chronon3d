// ---------------------------------------------------------------------------
// effect_blur.cpp — Box-filter Gaussian blur implementation
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip) {
    const i32 r = std::max(1, static_cast<i32>(std::round(radius)));
    const i32 w = fb.width(), h = fb.height();

    i32 x0 = 0, x1 = w;
    i32 y0 = 0, y1 = h;

    if (clip) {
        // Expand clip by radius to account for blur spread
        x0 = std::clamp(clip->x0 - r - 2, 0, w);
        x1 = std::clamp(clip->x1 + r + 2, 0, w);
        y0 = std::clamp(clip->y0 - r - 2, 0, h);
        y1 = std::clamp(clip->y1 + r + 2, 0, h);
    }

    if (x0 >= x1 || y0 >= y1) return;

    auto tmp_fb = acquire_temp_framebuffer(w, h);
    Framebuffer& tmp = *tmp_fb;

    for (int pass = 0; pass < 3; ++pass) {
        for (i32 y = y0; y < y1; ++y) {
            const Color* src_row = fb.pixels_row(y);
            Color* tmp_row = tmp.pixels_row(y);
            Color sum{0, 0, 0, 0};
            for (i32 x = x0 - r; x <= x0 + r; ++x) {
                const Color p = src_row[std::clamp(x, 0, w - 1)];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 x = x0; x < x1; ++x) {
                tmp_row[x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                const Color add = src_row[std::min(x + r + 1, w - 1)];
                const Color rem = src_row[std::max(x - r,     0)];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
        for (i32 x = x0; x < x1; ++x) {
            Color sum{0, 0, 0, 0};
            for (i32 y = y0 - r; y <= y0 + r; ++y) {
                const Color p = tmp.pixels_row(std::clamp(y, 0, h - 1))[x];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 y = y0; y < y1; ++y) {
                Color* fb_row = fb.pixels_row(y);
                fb_row[x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                const Color add = tmp.pixels_row(std::min(y + r + 1, h - 1))[x];
                const Color rem = tmp.pixels_row(std::max(y - r,     0))[x];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    }
}

} // namespace renderer
} // namespace chronon3d
