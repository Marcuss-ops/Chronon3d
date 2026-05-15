#include <chronon3d/renderer/software/software_renderer.hpp>
#include <algorithm>
#include <vector>

namespace chronon3d::software_internal {

namespace {

std::unique_ptr<Framebuffer> downsample_fb_local(const Framebuffer& src, i32 dst_w, i32 dst_h) {
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

} // namespace

std::unique_ptr<Framebuffer> render_frame(SoftwareRenderer& renderer, const Composition& comp,
                                          Frame frame) {
    const RenderSettings& settings = renderer.render_settings();
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);

    std::unique_ptr<Framebuffer> render_fb;

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        Scene scene = comp.evaluate(frame);
        render_fb = render_scene_internal(renderer, scene, comp.camera, rw, rh, frame, 0.0f);
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        for (int s = 0; s < N; ++s) {
            const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
            Scene sub_scene = comp.evaluate(frame, t);
            auto sub_fb = render_scene_internal(renderer, sub_scene, comp.camera, rw, rh, frame, t);

            for (int y = 0; y < rh; ++y) {
                for (int x = 0; x < rw; ++x) {
                    Color c = sub_fb->get_pixel(x, y);
                    const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                    accum[idx + 0] += c.r * weight;
                    accum[idx + 1] += c.g * weight;
                    accum[idx + 2] += c.b * weight;
                    accum[idx + 3] += c.a * weight;
                }
            }
        }

        render_fb = std::make_unique<Framebuffer>(rw, rh);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(
                    x, y, Color{accum[idx], accum[idx + 1], accum[idx + 2], accum[idx + 3]});
            }
        }
    }

    if (ssaa > 1.0f) {
        return downsample_fb_local(*render_fb, w, h);
    }
    return render_fb;
}

} // namespace chronon3d::software_internal
