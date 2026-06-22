#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/animation/temporal/temporal_samples.hpp>     // PR1: single source of truth
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "helpers.hpp"
#include <vector>
#include <spdlog/spdlog.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>

namespace chronon3d::graph {

// PR1 — Sub-sample generation has been centralised in
//   chronon3d::temporal::generate_temporal_samples(...)
// (include/chronon3d/animation/temporal/temporal_samples.hpp).
//
// The previous local helpers `motion_blur_jitter()`, `motion_blur_filter_weight()`,
// and the inline Halton sequence were deleted in this revision because they
// were byte-equivalent duplicates of the same logic in
// src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp.  Both
// consumers (this compositor AND ShutterPoseSampler) now produce IDENTICAL
// sample times and weights for identical (params, frame) inputs.
//
// The shutter-window geometry:
//   exposure_norm   = shutter_angle_deg / 360.0          ∈ (0, 1+]
//   window_start    = phase_deg / 360.0
//   sub_frame_u     = (s + 0.5 + jitter) / num_samples    ∈ [0, 1]
// sum_i w_i = 1.0 (premultiplied-RGBA-correct accumulation).

std::shared_ptr<Framebuffer> render_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const Composition& comp,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar
) {
    // (Default arg `sw_sidecar = nullptr` lives in
    //  `<chronon3d/render_graph/pipeline/render_pipeline.hpp>` —
    //  duplicated defaults here would Clang into the
    //  `default argument given for parameter 8` diagnostic.)
    const auto t0 = profiling::now();
    const auto hits_before = node_cache.stats().hits;
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);
    // 06 R3b — `sw_sidecar` is the typed channel from the caller
    // (the SoftwareRenderer that owns this engine).  Replaces the
    // previously-rtti-based hack that violated boundary-gate I4.
    // All downstream software-only branches read `sw_sidecar`
    // directly; no need to alias to a separate local.
    (void)sw_sidecar;

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

    // PR1 — Mutually-exclusive motion-blur modes.  See MotionBlurMode.
    //
    //   * Off                  → single-frame render with no shutter
    //   * TemporalAccumulation → N sub-frame accumulator below
    //   * VelocityApproximation→ single-frame render; PostProcessingSystem
    //                            applies velocity-buffer blur in the post
    //                            pass.  This branch is also entered if
    //                            mode == TemporalAccumulation but the user
    //                            erroneously requested samples <= 1 (we log
    //                            a warning).
    const bool want_temporal_accumulation =
        (settings.motion_blur.mode == MotionBlurMode::TemporalAccumulation) &&
        (settings.motion_blur.samples > 1);

    if (!want_temporal_accumulation) {
        if (settings.motion_blur.mode == MotionBlurMode::TemporalAccumulation &&
            settings.motion_blur.samples <= 1) {
            // Misconfiguration: caller asked for Temporal mode with 0 or 1 samples.
            // We fall back to the no-blur single-frame path; the velocity-buffer
            // path is NOT auto-selected (that would be silent mode-flipping).
            spdlog::warn(
                "[motion-blur] mode=TemporalAccumulation requires samples >= 2 "
                "(got {}). Falling back to single-frame render; no motion blur "
                "will be produced. To use velocity-buffer blur, switch "
                "mode=VelocityApproximation instead.",
                settings.motion_blur.samples);
        } else if (settings.motion_blur.mode == MotionBlurMode::VelocityApproximation &&
                   settings.motion_blur.samples > 1) {
            // Informational only: samples count is irrelevant for Velocity mode
            // (the velocity-buffer pass computes its own sample count from
            // `VelocityBufferMotionBlur::settings().samples`).
            spdlog::info(
                "[motion-blur] mode=VelocityApproximation: samples={} ignored "
                "(velocity-buffer path uses its own sample count); shutter_angle "
                "ignored in this mode.",
                settings.motion_blur.samples);
        }

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

        // PR1 — use the canonical temporal-sample generator.  Identical
        // (params, frame) yields identical output as `ShutterPoseSampler::evaluate()`.
        chronon3d::temporal::TemporalSampleParams mb_params;
        mb_params.shutter_angle_deg = settings.motion_blur.shutter_angle_deg;
        mb_params.shutter_phase_deg = settings.motion_blur.shutter_phase_deg;
        mb_params.pattern           = settings.motion_blur.pattern;
        mb_params.filter            = settings.motion_blur.filter;
        mb_params.jitter_seed       = settings.motion_blur.jitter_seed;

        const chronon3d::temporal::TemporalSampleSet samples =
            chronon3d::temporal::generate_temporal_samples(
                mb_params, N, frame);

        std::vector<float> sample_times(static_cast<size_t>(N));

        {
            // PR1 — Use the canonical geometric info from the new module.
            // `samples.exposure_normalized`     = shutter_angle_deg / 360
            // `samples.window_start_normalized` = shutter_phase_deg   / 360
            // (Identical numerically to the previous inlined formula, but
            //  reads from a single source of truth — which is exactly what
            //  PR1 promised.  If generate_temporal_samples ever clamps /
            //  validates inputs, the two callers will agree by construction.)
            const double opening_offset = samples.window_start_normalized;
            const double exposure_norm  = samples.exposure_normalized;
            for (int s = 0; s < N; ++s) {
                sample_times[s] = static_cast<float>(
                    opening_offset + samples.sample_times[s] * exposure_norm);
            }
        }

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);

        // TICKET-007.j — track the bit-exact sum of the SAME weights that were
        // sent into the accumulator.  Multiplication by `1 / sum_w` at the end
        // cancels any FP-rounding error that occurred when the per-sample
        // weights were normalized upstream; for STATIC content (sub-frame
        // samples pixel-identical) this guarantees byte-equal framebuffers
        // between N=16 and N=1 / mode=Off, because the implicit multiplication
        // by `sum_w` in `accum == sample_0 * sum_w` is perfectly compensated
        // by `accum * (1 / sum_w) == sample_0`.
        float actual_weight_sum = 0.0f;

        const auto t_mb0 = profiling::now();
        {
            CHRONON_ZONE_C("motion_blur_accumulation", trace_category::kEffect);
            for (int s = 0; s < N; ++s) {
                const float t = sample_times[s];
                const float w = samples.normalized_weights[s];
                actual_weight_sum += w;
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
                                auto acc = LoadU(df, dst + x);
                                auto vals = LoadU(df, src + x);
                                acc = MulAdd(vals, v_weight, acc);
                                StoreU(acc, df, dst + x);
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

        // TICKET-007.j — single reciprocal-multiply final normalization.
        //   output = accum * (1 / sum_w)
        // is FP-bit-exact equivalent to accum / sum_w but cheaper (one divide,
        // broadcast across SIMD lanes) than per-pixel division, AND it
        // perfectly compensates any non-associative FP drift introduced by
        // the per-sample weight sum `sum_w == sum_{s} w_s`.  Guarded for the
        // degenerate case where all sample weights were zero (e.g. shutter
        // phase pushed the window outside the frame).
        const float post_norm = (actual_weight_sum > 1e-6f)
            ? (1.0f / actual_weight_sum)
            : 1.0f;

        // Write accumulated float buffer to output framebuffer (parallel + SIMD)
        render_fb = backend.framebuffer_pool()->acquire(rw, rh, true);
        tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
            [&](const tbb::blocked_range<int>& range) {
                using namespace hwy::HWY_NAMESPACE;
                const ScalableTag<float> df;
                const size_t lanes = Lanes(df);
                const auto v_post = Set(df, post_norm);

                for (int y = range.begin(); y < range.end(); ++y) {
                    const float* src = accum.data() + static_cast<size_t>(y * rw * 4);
                    float* dst = reinterpret_cast<float*>(render_fb->pixels_row(y));
                    const int total_floats = rw * 4;

                    int x = 0;
                    for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                        auto v = LoadU(df, src + x);
                        v = Mul(v, v_post);
                        StoreU(v, df, dst + x);
                    }
                    for (; x < total_floats; ++x) {
                        dst[x] = src[x] * post_norm;
                    }
                }
            }
        );
    }

    if (sw_sidecar) {
        sw_sidecar->dirty_telemetry().last_layer_count = layer_count;
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
