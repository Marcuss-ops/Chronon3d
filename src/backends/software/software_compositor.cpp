#include <chronon3d/backends/software/software_compositor.hpp>

namespace chronon3d {

void SoftwareCompositor::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    if (mode == BlendMode::Normal) {
        if (composite_layer_normal_optimized(dst, src)) {
            return;
        }
    }

    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            s = s.unpremultiplied();
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src) {
    const i32 w = dst.width();
    const i32 h = dst.height();
    if (src.width() != w || src.height() != h) return false;

    for (i32 y = 0; y < h; ++y) {
        Color* d_row = dst.pixels_row(y);
        const Color* s_row = src.pixels_row(y);
        for (i32 x = 0; x < w; ++x) {
            const Color& s = s_row[x];
            if (s.a <= 0.0f) continue;
            Color& d = d_row[x];
            // Normal blend (premultiplied): dst = src + dst * (1 - src.a)
            const float inv_a = 1.0f - s.a;
            d.r = s.r + d.r * inv_a;
            d.g = s.g + d.g * inv_a;
            d.b = s.b + d.b * inv_a;
            d.a = s.a + d.a * inv_a;
        }
    }
    return true;
}

} // namespace chronon3d
