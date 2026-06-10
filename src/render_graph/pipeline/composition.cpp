#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "helpers.hpp"
#include <chrono>
#include <vector>
#include <spdlog/spdlog.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>

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

                // Parallel accumulation with TBB + Highway SIMD
                // Each row is independent — no race conditions.
                tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
                    [&](const tbb::blocked_range<int>& range) {
                        using namespace hwy::HWY_NAMESPACE;
                        const ScalableTag<float> df;
                        const size_t lanes = Lanes(df);
                        const auto v_weight = Set(df, weight);

                        for (int y = range.begin(); y < range.end(); ++y) {
                            const float* src = reinterpret_cast<const float*>(sub_fb.pixels_row(y));
                            float* dst = accum.data() + static_cast<size_t>(y * rw * 4);
                            const int total_floats = rw * 4;

                            // SIMD path: process full lanes
                            int x = 0;
                            for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                                auto acc = Load(df, dst + x);
                                auto vals = Load(df, src + x);
                                acc = MulAdd(vals, v_weight, acc);
                                Store(acc, df, dst + x);
                            }
                            // Scalar tail for remaining floats
                            for (; x < total_floats; ++x) {
                                dst[x] += src[x] * weight;
                            }
                        }
                    }
                );
            }
        }
        motion_blur_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_mb0).count();

        // Write accumulated float buffer to output framebuffer (parallel + SIMD)
        render_fb = backend.framebuffer_pool()->acquire(rw, rh, true);
        tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
            [&](const tbb::blocked_range<int>& range) {
                using namespace hwy::HWY_NAMESPACE;
                const ScalableTag<float> df;
                const size_t lanes = Lanes(df);

                for (int y = range.begin(); y < range.end(); ++y) {
                    const float* src = accum.data() + static_cast<size_t>(y * rw * 4);
                    float* dst = reinterpret_cast<float*>(render_fb->pixels_row(y));
                    const int total_floats = rw * 4;

                    int x = 0;
                    for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                        Store(Load(df, src + x), df, dst + x);
                    }
                    for (; x < total_floats; ++x) {
                        dst[x] = src[x];
                    }
                }
            }
        );
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
        return std::shared_ptr<Framebuffer>(out.release());
    }

    return render_fb;
}

} // namespace chronon3d::graph
