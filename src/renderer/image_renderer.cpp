#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chronon3d/renderer/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask_utils.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

namespace {

struct LoadedImage {
    int width{0};
    int height{0};
    int channels{0};
    unsigned char* pixels{nullptr};

    LoadedImage() = default;
    
    // Non-copyable
    LoadedImage(const LoadedImage&) = delete;
    LoadedImage& operator=(const LoadedImage&) = delete;

    ~LoadedImage() {
        if (pixels) {
            stbi_image_free(pixels);
        }
    }
};

} // namespace

bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    LoadedImage loaded;
    loaded.pixels = stbi_load(image.path.c_str(), &loaded.width, &loaded.height, &loaded.channels, 4);

    if (!loaded.pixels || loaded.width <= 0 || loaded.height <= 0) {
        return false;
    }

    const Mat4& model = state.matrix;
    Mat4 inv_model = glm::inverse(model);

    // Local image rect is [0, size.x] x [0, size.y]
    Vec4 corners[4] = {
        model * Vec4(0, 0, 0, 1),
        model * Vec4(image.size.x, 0, 0, 1),
        model * Vec4(image.size.x, image.size.y, 0, 1),
        model * Vec4(0, image.size.y, 0, 1)
    };

    f32 min_x = corners[0].x;
    f32 max_x = corners[0].x;
    f32 min_y = corners[0].y;
    f32 max_y = corners[0].y;

    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }

    i32 x0 = std::max<i32>(0, static_cast<i32>(std::floor(min_x)));
    i32 y0 = std::max<i32>(0, static_cast<i32>(std::floor(min_y)));
    i32 x1 = std::min<i32>(fb.width(), static_cast<i32>(std::ceil(max_x)));
    i32 y1 = std::min<i32>(fb.height(), static_cast<i32>(std::ceil(max_y)));

    if (x0 >= x1 || y0 >= y1) {
        return true;
    }

    const f32 final_opacity = image.opacity * state.opacity;

    if (final_opacity <= 0.0f) {
        return true;
    }

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            Vec4 local = inv_model * Vec4(static_cast<f32>(x) + 0.5f,
                                          static_cast<f32>(y) + 0.5f,
                                          0.0f,
                                          1.0f);

            if (local.x < 0.0f || local.y < 0.0f ||
                local.x >= image.size.x || local.y >= image.size.y) {
                continue;
            }

            if (state.mask && state.mask->enabled()) {
                Vec4 layer_local = state.layer_inv_matrix *
                                   Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
                if (!mask_contains_local_point(*state.mask, Vec2{layer_local.x, layer_local.y})) continue;
            }

            f32 u = local.x / image.size.x;
            f32 v = local.y / image.size.y;

            int sx = std::clamp(static_cast<int>(u * loaded.width), 0, loaded.width - 1);
            int sy = std::clamp(static_cast<int>(v * loaded.height), 0, loaded.height - 1);

            const int idx = (sy * loaded.width + sx) * 4;

            Color src{
                loaded.pixels[idx + 0] / 255.0f,
                loaded.pixels[idx + 1] / 255.0f,
                loaded.pixels[idx + 2] / 255.0f,
                (loaded.pixels[idx + 3] / 255.0f) * final_opacity
            };

            if (src.a <= 0.0f) continue;

            fb.set_pixel(x, y, compositor::blend(src, fb.get_pixel(x, y), BlendMode::Normal));
        }
    }

    return true;
}

} // namespace chronon3d
