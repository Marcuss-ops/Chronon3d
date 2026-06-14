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
#include <vector>
#include <spdlog/spdlog.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>

namespace chronon3d::graph {

// ── Deterministic jitter for motion blur sub-samples ───────────────────────
// Produces a value in [-0.5, 0.5) based on seed, frame, sample index.
// Same inputs always produce the same output (deterministic).
[[nodiscard]] static double motion_blur_jitter(
    TemporalSamplePattern pattern, u64 seed, int frame, int sample_idx, int total_samples)
{
    switch (pattern) {
        case TemporalSamplePattern::Uniform:
            return 0.0;
        case TemporalSamplePattern::Stratified: {
            // Hash-based deterministic jitter: map to [0, 1) then shift to [-0.5, 0.5)
            uint64_t h = seed;
            h ^= static_cast<uint64_t>(frame) * 0x9E3779B97F4A7C15ULL;
            h ^= static_cast<uint64_t>(sample_idx) * 0xBF58476D1CE4E5B9ULL;
            h ^= static_cast<uint64_t>(total_samples) * 0x94D049BB133111EBULL;
            h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
            h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
            h = h ^ (h >> 31);
            const double u = static_cast<double>(h & 0x7FFFFFFFFFFFFFFFULL)
                           / static_cast<double>(0x8000000000000000ULL);
            return u - 0.5;  // centre in [-0.5, 0.5)
        }
        case TemporalSamplePattern::Halton: {
            // Halton sequence (base 2) for low-discrepancy sampling
            double h_val = 0.0;
            double f = 0.5;
            int n = sample_idx + 1;
            while (n > 0) {
                h_val += f * static_cast<double>(n & 1);
                n >>= 1;
                f *= 0.5;
            }
            return h_val - 0.5;  // centre in [-0.5, 0.5)
        }
    }
    return 0.0;
}

// ── Reconstruction filter weight ──────────────────────────────────────────
// Returns the filter weight at normalised time t ∈ [0, 1].
// Weights are normalised later by the caller.
[[nodiscard]] static double motion_blur_filter_weight(TemporalFilter filter, double t) {
    switch (filter) {
        case TemporalFilter::Box:
            return 1.0;
        case TemporalFilter::Triangle:
            return 1.0 - std::abs(2.0 * t - 1.0);
        case TemporalFilter::Gaussian: {
            constexpr double kSigma = 0.25;  // exposure_duration / 4
            const double z = (t - 0.5) / kSigma;
            return std::exp(-0.5 * z * z);
        }
    }
    return 1.0;
}

std::shared_ptr<Framebuffer> render_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const Composition& comp,
    Frame frame
) {
    const auto t0 = profiling::now();
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
        const auto t_eval0 = profiling::now();
        Scene scene;
        {
            CHRONON_ZONE_C("evaluate_composition", trace_category::kTimeline);
            scene = comp.evaluate(frame);
        }
        evaluate_ms = profiling::duration_ms(t_eval0, profiling::now());
        layer_count = static_cast<int>(scene.layers().size());

        const auto t_scene0 = profiling::now();
        {
            CHRONON_ZONE_C("render_scene_graph", trace_category::kGraph);
            render_fb = call_graph(scene, frame, 0.0f);
        }
        scene_ms = profiling::duration_ms(t_scene0, profiling::now());
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const double exposure_frames = static_cast<double>(settings.motion_blur.shutter_angle_deg) / 360.0;
        const double opening_offset   = static_cast<double>(settings.motion_blur.shutter_phase_deg) / 360.0;
        const u64   jitter_seed       = settings.motion_blur.jitter_seed;
        const TemporalSamplePattern pattern = settings.motion_blur.pattern;
        const TemporalFilter        filter  = settings.motion_blur.filter;

        // ── Pre-compute sample times and normalised filter weights ──────
        std::vector<double> sample_times(static_cast<size_t>(N));
        std::vector<float>  sample_weights(static_cast<size_t>(N));
        {
            double weight_sum = 0.0;
            for (int s = 0; s < N; ++s) {
                const double j = motion_blur_jitter(pattern, jitter_seed,
                    static_cast<int>(frame), s, N);
                const double u = std::clamp(
                    (static_cast<double>(s) + 0.5 + j) / static_cast<double>(N),
                    0.0, 1.0);
                sample_times[s] = opening_offset + u * exposure_frames;

                const double raw_w  = std::max(0.0, motion_blur_filter_weight(filter, u));
                sample_weights[s] = static_cast<float>(raw_w);
                weight_sum += raw_w;
            }
            // Normalise so weights sum to 1.0 (correct alpha accumulation)
            const float inv_sum = (weight_sum > 0.0)
                ? static_cast<float>(1.0 / weight_sum) : 1.0f;
            for (int s = 0; s < N; ++s) {
                sample_weights[s] *= inv_sum;
            }
        }

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);

        const auto t_mb0 = profiling::now();
        {
            CHRONON_ZONE_C("motion_blur_accumulation", trace_category::kEffect);
            for (int s = 0; s < N; ++s) {
                const float t = static_cast<float>(sample_times[s]);
                const float w = sample_weights[s];
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
                        const auto v_weight = Set(df, w);

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
                                dst[x] += src[x] * w;
                            }
                        }
                    }
                );
            }
        }
        motion_blur_ms = profiling::duration_ms(t_mb0, profiling::now());

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
        sw_renderer->dirty_telemetry().last_layer_count = layer_count;
    }

    if (ssaa > 1.0f) {
        const auto t_down0 = profiling::now();
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
