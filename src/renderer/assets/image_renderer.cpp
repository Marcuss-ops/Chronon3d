#include <chronon3d/renderer/assets/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask_utils.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    const CachedImage* cached = m_cache.get_or_load(image.path);
    if (!cached) return false;

    const Mat4& model    = state.matrix;
    const Mat4  inv_model = glm::inverse(model);

    Vec4 corners[4] = {
        model * Vec4(0, 0, 0, 1),
        model * Vec4(image.size.x, 0, 0, 1),
        model * Vec4(image.size.x, image.size.y, 0, 1),
        model * Vec4(0, image.size.y, 0, 1)
    };

    f32 min_x = corners[0].x, max_x = corners[0].x;
    f32 min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x); max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y); max_y = std::max(max_y, corners[i].y);
    }

    const i32 x0 = std::max<i32>(0, static_cast<i32>(std::floor(min_x)));
    const i32 y0 = std::max<i32>(0, static_cast<i32>(std::floor(min_y)));
    const i32 x1 = std::min<i32>(fb.width(),  static_cast<i32>(std::ceil(max_x)));
    const i32 y1 = std::min<i32>(fb.height(), static_cast<i32>(std::ceil(max_y)));

    if (x0 >= x1 || y0 >= y1) return true;

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.0f) return true;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            Vec4 local = inv_model * Vec4(static_cast<f32>(x) + 0.5f,
                                          static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);

            if (local.x < 0.0f || local.y < 0.0f ||
                local.x >= image.size.x || local.y >= image.size.y) {
                continue;
            }

            if (state.mask && state.mask->enabled()) {
                Vec4 ll = state.layer_inv_matrix *
                          Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
                if (!mask_contains_local_point(*state.mask, Vec2{ll.x, ll.y})) continue;
            }

            const f32 u  = local.x / image.size.x;
            const f32 v  = local.y / image.size.y;
            const int sx = std::clamp(static_cast<int>(u * cached->width),  0, cached->width  - 1);
            const int sy = std::clamp(static_cast<int>(v * cached->height), 0, cached->height - 1);
            const int idx = (sy * cached->width + sx) * 4;

            Color src{
                cached->pixels[idx + 0] / 255.0f,
                cached->pixels[idx + 1] / 255.0f,
                cached->pixels[idx + 2] / 255.0f,
                (cached->pixels[idx + 3] / 255.0f) * final_opacity
            };

            if (src.a <= 0.0f) continue;
            fb.set_pixel(x, y, compositor::blend(src, fb.get_pixel(x, y), BlendMode::Normal));
        }
    }
    return true;
}

} // namespace chronon3d
