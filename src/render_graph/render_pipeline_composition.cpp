#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "builder/graph_builder_pipeline.hpp"
#include "builder/graph_builder_internal.hpp"
#include "render_pipeline_helpers.hpp"
#include <chrono>
#include <vector>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    const Composition& comp,
    Frame frame
) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    std::shared_ptr<Framebuffer> render_fb = nullptr;

    double evaluate_ms = 0.0;
    double scene_ms = 0.0;
    double motion_blur_ms = 0.0;
    double downsample_ms = 0.0;
    int layer_count = 0;

    auto call_graph = [&](const Scene& scene, Frame fr, f32 t) {
        return render_scene_via_graph(
            backend, node_cache, scene, comp.camera, rw, rh, fr, t, settings, registry, video_decoder,
            static_cast<float>(comp.frame_rate().fps()),
            comp.name()
        );
    };

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        const auto t_eval0 = std::chrono::steady_clock::now();
        Scene scene;
        {
            CHRONON_ZONE_C("evaluate_composition", trace_category::kTimeline);
            scene = comp.evaluate(frame);
        }
        evaluate_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_eval0).count();
        layer_count = static_cast<int>(scene.layers().size());

        const auto t_scene0 = std::chrono::steady_clock::now();
        {
            CHRONON_ZONE_C("render_scene_graph", trace_category::kGraph);
            render_fb = call_graph(scene, frame, 0.0f);
        }
        scene_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_scene0).count();
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;
        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        const auto t_mb0 = std::chrono::steady_clock::now();
        {
            CHRONON_ZONE_C("motion_blur_accumulation", trace_category::kEffect);
            for (int s = 0; s < N; ++s) {
                const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
                Scene sub = comp.evaluate(frame, t);
                if (s == 0) layer_count = static_cast<int>(sub.layers().size());
                const Framebuffer& sub_fb = *call_graph(sub, frame, t);
                for (int y = 0; y < rh; ++y) {
                    for (int x = 0; x < rw; ++x) {
                        const Color c = sub_fb.get_pixel(x, y);
                        const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                        accum[idx + 0] += c.r * weight;
                        accum[idx + 1] += c.g * weight;
                        accum[idx + 2] += c.b * weight;
                        accum[idx + 3] += c.a * weight;
                    }
                }
            }
        }
        motion_blur_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_mb0).count();

        // render_fb is acquired and filled with the accumulated values.
        render_fb = backend.framebuffer_pool()->acquire(rw, rh, true);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
            }
        }
    }

    if (sw_renderer) {
        sw_renderer->m_last_layer_count = layer_count;
    }

    if (ssaa > 1.0f) {
        const auto t_down0 = std::chrono::steady_clock::now();
        std::unique_ptr<Framebuffer> out;
        {
            CHRONON_ZONE_C("downsample_ssaa", trace_category::kDownsample);
            out = downsample_fb(*render_fb, w, h);
        }
        downsample_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_down0).count();
        telemetry::record_render_telemetry(make_telemetry_row(
            "frame_render",
            frame,
            w,
            h,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
            evaluate_ms,
            scene_ms,
            motion_blur_ms,
            downsample_ms,
            node_cache.stats().hits > hits_before ? 1 : 0,
            layer_count,
            backend.counters()
        ));
        return std::shared_ptr<Framebuffer>(out.release());
    }

    telemetry::record_render_telemetry(make_telemetry_row(
        "frame_render",
        frame,
        w,
        h,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
        evaluate_ms,
        scene_ms,
        motion_blur_ms,
        downsample_ms,
        node_cache.stats().hits > hits_before ? 1 : 0,
        layer_count,
        backend.counters()
    ));
    return render_fb;
}

} // namespace chronon3d::graph
