#include "primitive_renderer.hpp"

namespace chronon3d {
namespace renderer {

std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h) {
    auto dst = std::make_unique<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width()) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            int x0 = static_cast<int>(x * sx);
            int y0 = static_cast<int>(y * sy);
            int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());

            for (int sy_i = y0; sy_i < y1; ++sy_i) {
                for (int sx_i = x0; sx_i < x1; ++sx_i) {
                    Color c = src.get_pixel(sx_i, sy_i);
                    r += c.r;
                    g += c.g;
                    b += c.b;
                    a += c.a;
                    count++;
                }
            }
            if (count > 0) {
                float inv = 1.0f / count;
                dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
            }
        }
    }
    return dst;
}

} // namespace renderer
} // namespace chronon3d
