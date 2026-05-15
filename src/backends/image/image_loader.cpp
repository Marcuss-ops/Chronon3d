#include <chronon3d/backends/image/image_loader.hpp>
#include <stb_image.h>
#include <algorithm>

namespace chronon3d::io {

std::shared_ptr<Framebuffer> load_image_as_framebuffer(
    const std::string& path,
    i32 target_width,
    i32 target_height
) {
    int width, height, channels;
    unsigned char* raw = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if (!raw) {
        return nullptr;
    }

    const i32 w = (target_width > 0)  ? target_width  : width;
    const i32 h = (target_height > 0) ? target_height : height;

    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color::transparent());

    // Simple nearest-neighbor scale for the loader (the video extractor will handle scaling via ffmpeg usually)
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            const int sx = std::clamp(static_cast<int>((static_cast<f32>(x) / w) * width),  0, width  - 1);
            const int sy = std::clamp(static_cast<int>((static_cast<f32>(y) / h) * height), 0, height - 1);
            const int idx = (sy * width + sx) * 4;

            Color c{
                raw[idx + 0] / 255.0f,
                raw[idx + 1] / 255.0f,
                raw[idx + 2] / 255.0f,
                raw[idx + 3] / 255.0f
            };
            fb->set_pixel(x, y, c.to_linear());
        }
    }

    stbi_image_free(raw);
    return fb;
}

} // namespace chronon3d::io
