#include <chronon3d/backends/software/software_renderer.hpp>
#include "software_pipeline_private.hpp"
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <algorithm>
#include <chrono>
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
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = renderer.node_cache().stats().hits;
    const RenderSettings& settings = renderer.render_settings();
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);

    std::unique_ptr<Framebuffer> render_fb;
    double evaluate_ms = 0.0;
    double scene_ms = 0.0;
    double motion_blur_ms = 0.0;
    double downsample_ms = 0.0;
    int layer_count = 0;

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        const auto t_eval0 = std::chrono::steady_clock::now();
        Scene scene = comp.evaluate(frame);
        const auto t_eval1 = std::chrono::steady_clock::now();
        evaluate_ms = std::chrono::duration<double, std::milli>(t_eval1 - t_eval0).count();
        layer_count = static_cast<int>(scene.layers().size());

        const auto t_scene0 = std::chrono::steady_clock::now();
        auto fb_shared = graph::render_scene_via_graph(
            renderer,
            renderer.node_cache(),
            scene,
            comp.camera,
            rw,
            rh,
            frame,
            0.0f,
            renderer.render_settings(),
            renderer.composition_registry(),
            renderer.video_decoder()
        );
        render_fb = std::make_unique<Framebuffer>(*fb_shared);
        const auto t_scene1 = std::chrono::steady_clock::now();
        scene_ms = std::chrono::duration<double, std::milli>(t_scene1 - t_scene0).count();
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        const auto t_mb0 = std::chrono::steady_clock::now();
        for (int s = 0; s < N; ++s) {
            const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
            Scene sub_scene = comp.evaluate(frame, t);
            if (s == 0) {
                layer_count = static_cast<int>(sub_scene.layers().size());
            }
            auto sub_fb_shared = graph::render_scene_via_graph(
                renderer,
                renderer.node_cache(),
                sub_scene,
                comp.camera,
                rw,
                rh,
                frame,
                t,
                renderer.render_settings(),
                renderer.composition_registry(),
                renderer.video_decoder()
            );
            auto sub_fb = std::make_unique<Framebuffer>(*sub_fb_shared);

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
        const auto t_mb1 = std::chrono::steady_clock::now();
        motion_blur_ms = std::chrono::duration<double, std::milli>(t_mb1 - t_mb0).count();

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
        const auto t_down0 = std::chrono::steady_clock::now();
        auto out = downsample_fb_local(*render_fb, w, h);
        const auto t_down1 = std::chrono::steady_clock::now();
        downsample_ms = std::chrono::duration<double, std::milli>(t_down1 - t_down0).count();
        const auto t1 = std::chrono::steady_clock::now();
        telemetry::record_render_telemetry({
            .event = "frame_render",
            .frame = frame,
            .width = w,
            .height = h,
            .total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
            .setup_ms = evaluate_ms,
            .composite_ms = scene_ms,
            .blur_ms = motion_blur_ms,
            .encode_ms = downsample_ms,
            .cache_hit = renderer.node_cache().stats().hits > hits_before ? 1 : 0,
            .layer_count = layer_count,
        });
        return out;
    }
    const auto t1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry({
        .event = "frame_render",
        .frame = frame,
        .width = w,
        .height = h,
        .total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
        .setup_ms = evaluate_ms,
        .composite_ms = scene_ms,
        .blur_ms = motion_blur_ms,
        .encode_ms = downsample_ms,
        .cache_hit = renderer.node_cache().stats().hits > hits_before ? 1 : 0,
        .layer_count = layer_count,
    });
    return render_fb;
}

} // namespace chronon3d::software_internal
