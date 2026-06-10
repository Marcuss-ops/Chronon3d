#pragma once

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <xxhash.h>
#include <memory>
#include <string>

namespace chronon3d::test {

inline SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    return renderer;
}

inline SoftwareRenderer make_renderer_ssaa(float factor) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.ssaa_factor = std::max(1.0f, factor);
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    return renderer;
}

inline u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

inline FrameContext make_ctx(Frame frame, int width = 1920, int height = 1080) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = FrameRate{30, 1};
    ctx.width = width;
    ctx.height = height;
    return ctx;
}

inline float average_luma_rect(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    float total = 0.0f;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            total += (c.r * 0.299f + c.g * 0.587f + c.b * 0.114f) * c.a;
            count++;
        }
    }
    return count > 0 ? total / (float)count : 0.0f;
}

inline std::shared_ptr<Framebuffer> render_fn(
    std::function<Scene(const FrameContext&)> build_fn,
    int width = 200,
    int height = 200
) {
    SoftwareRenderer renderer;
    Composition comp = composition({.width = width, .height = height}, build_fn);
    return renderer.render_frame(comp, 0);
}

inline std::shared_ptr<Framebuffer> load_png_as_framebuffer(const std::string& path) {
    image::StbImageBackend backend;
    auto buf = backend.load_image(path);
    if (!buf) return nullptr;
    auto fb = std::make_shared<Framebuffer>(buf->width, buf->height);
    for (int y = 0; y < buf->height; ++y) {
        for (int x = 0; x < buf->width; ++x) {
            int idx = (y * buf->width + x) * 4;
            Color c{
                static_cast<float>(buf->pixels[idx]) / 255.0f,
                static_cast<float>(buf->pixels[idx+1]) / 255.0f,
                static_cast<float>(buf->pixels[idx+2]) / 255.0f,
                static_cast<float>(buf->pixels[idx+3]) / 255.0f
            };
            fb->set_pixel(x, y, c);
        }
    }
    return fb;
}

} // namespace chronon3d::test
