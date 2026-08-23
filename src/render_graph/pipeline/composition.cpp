#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/animation/temporal/temporal_samples.hpp>     // PR1: single source of truth
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include "temporal_render_pipeline.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "helpers.hpp"
#include <optional>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>


// ═══════════════════════════════════════════════════════════════════════════
// Refactor 6 rationale — composition.cpp is intentionally kept whole.
// ═══════════════════════════════════════════════════════════════════════════
//
// The user's original plan suggested splitting this file into:
//     composition_pipeline.cpp / scene_graph_builder.cpp /
//     frame_graph_compile_stage.cpp / render_execution_stage.cpp /
//     render_output_stage.cpp / pipeline_diagnostics.cpp
//
// After review we decided NOT to split. The file is ONE function
// (`render_compiled_composition_frame`) with three control-flow branches that
// share state throughout the call:
//
//   * single-frame render (no motion blur)
//   * temporal accumulation  (N sub-frames pooled into the same buffer)
//   * SSAA downsample + final return
//
// The branches are NOT distinct pipeline stages. They share:
//   - `render_fb`            (accumulator becomes the final output in
//                             the temporal branch — same pooled buffer
//                             pool::acquire on entry, returned on exit)
//   - `samples`, `actual_weight_sum`, `sample_times`
//                            (only used in the temporal branch but
//                             declared at function scope so they can be
//                             hoisted if needed)
//   - profiling counters     (CHRONON_TRACE_SCOPE slices wrap each branch's
//                             hot path; moving them across files would
//                             break the parent-slice hierarchy)
//   - the engine pointer      (`frame_engine` is forwarded to
//                             `Composition::evaluate` from both branches
//                             for textual consistency)
//
// A 6-file pipeline-stage split would require shuttling 10+ parameters
// (render_fb, samples, sum_w, telemetry, ssaa state, h, rw, rh, …) across
// ABI seams. The CMotionBlur reciprocal-multiply normalisation explicitly
// applies `1 / actual_weight_sum` to the SAME buffer that holds the
// accumulated sub-frames — separating "accumulate" from "normalise" into
// distinct Tuples would force a full-frame copy pass that the in-place
// path explicitly avoids for memory-bandwidth reasons (~66 MiB at 1080p
// ssaa=1, ~264 MiB at ssaa=2 / 4K).
//
// Decision: leave this file whole. If a future profiler flags a hotspot
// in a SPECIFIC sub-section (e.g. SSAA downsample only) we'll address it
// surgically, not as an across-the-board split.
// ═══════════════════════════════════════════════════════════════════════════

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

std::shared_ptr<Framebuffer> render_compiled_composition_frame_temporal(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const CompiledComposition& compiled,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar,
    chronon3d::CancellationToken* cancellation
) {
    if (!compiled.definition) {
        throw std::invalid_argument(
            "compiled composition has no definition");
    }
    const auto& spec = compiled.definition->composition;
    // Materialise the RenderRuntime* once at the entry point so both the
    // single-frame and the motion-blur sub-frame evaluation paths below
    // share the same pointer.  Primary source: sw_sidecar (the
    // SoftwareRenderer that owns the per-instance runtime).
    // When sw_sidecar is nullptr (CLI dry-run, test paths), frame_runtime
    // is null — the materializer's resolve_engine() provides a process-wide
    // fallback (F1.D) so text still renders.
    const chronon3d::runtime::RenderRuntime* frame_runtime =
        (sw_sidecar != nullptr) ? &sw_sidecar->runtime() : nullptr;
    const auto hits_before = node_cache.stats().hits;
    if (!std::isfinite(settings.ssaa_factor)) {
        throw std::invalid_argument("SSAA factor must be finite");
    }
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = spec.width;
    const int h = spec.height;
    if (w <= 0 || h <= 0 || !std::isfinite(ssaa)) {
        throw std::invalid_argument("render dimensions and SSAA must be finite and positive");
    }
    const double scaled_width = static_cast<double>(w) * static_cast<double>(ssaa);
    const double scaled_height = static_cast<double>(h) * static_cast<double>(ssaa);
    if (scaled_width > static_cast<double>(std::numeric_limits<int>::max()) ||
        scaled_height > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("SSAA render dimensions exceed the integer framebuffer limit");
    }
    const int rw = static_cast<int>(scaled_width);
    const int rh = static_cast<int>(scaled_height);
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

    auto call_graph = [&](const Scene& scene,
                          Frame fr,
                          f32 t,
                          const TemporalRenderContext* temporal_context = nullptr) {
        // The scene camera is the sole authored runtime camera state.
        // The graph receives a neutral context camera for plain 2D scenes;
        // authored Camera2_5D state is applied by the scene itself.
        Camera context_camera{};
        if (temporal_context) {
            return render_scene_via_graph_temporal(
                backend, node_cache, scene, context_camera,
                rw, rh, fr, t, settings, registry, video_decoder,
                static_cast<float>(spec.frame_rate.fps()), spec.name,
                sw_sidecar, temporal_context);
        }
        return render_scene_via_graph(
            backend, node_cache, scene, context_camera,
            rw, rh, fr, t, settings, registry, video_decoder,
            static_cast<float>(spec.frame_rate.fps()), spec.name, sw_sidecar);
    };

    auto evaluate_scene = [&](const FrameContext& context) {
        auto evaluated = chronon3d::evaluate(
            compiled,
            CompositionEvaluateContext{.frame_context = context},
            context.local_time());
        if (!evaluated) {
            throw std::runtime_error(
                "compiled composition evaluation failed: " +
                evaluated.error().message);
        }
        const auto camera = evaluated.value().camera;
        Scene scene = std::move(evaluated.value().scene);
        if (camera.has_value()) {
            scene.set_camera_2_5d(*camera);
        }
        return scene;
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
        (settings.motion_blur.samples > 1) &&
        (settings.motion_blur.shutter_angle_deg > 0.0f);

    if (!want_temporal_accumulation) {
        if (cancellation && cancellation->is_cancelled()) {
            return nullptr;
        }
        if (settings.motion_blur.mode == MotionBlurMode::TemporalAccumulation &&
            settings.motion_blur.samples <= 1) {
            spdlog::warn(
                "[motion-blur] mode=TemporalAccumulation requires samples >= 2 "
                "(got {}). Falling back to single-frame render; no motion blur "
                "will be produced. To use velocity-buffer blur, switch "
                "mode=VelocityApproximation instead.",
                settings.motion_blur.samples);
        } else if (settings.motion_blur.mode == MotionBlurMode::VelocityApproximation &&
                   settings.motion_blur.samples > 1) {
            spdlog::info(
                "[motion-blur] mode=VelocityApproximation: samples={} ignored "
                "(velocity-buffer path uses its own sample count); shutter_angle "
                "ignored in this mode.",
                settings.motion_blur.samples);
        }

        const auto t_eval0 = profiling::now();
        Scene dynamic_scene;
        const Scene* scene_ptr = nullptr;
        if (compiled.is_static_topology && compiled.template_scene) {
            scene_ptr = compiled.template_scene.get();
        } else {
            CHRONON_TRACE_SCOPE("chronon.frame", "evaluate_composition");
            const FrameContext ctx = make_frame_context({
                .global_time = SampleTime::from_frame(static_cast<double>(frame), spec.frame_rate),
                .duration = spec.duration,
                .width = spec.width,
                .height = spec.height,
                .assets_root = frame_runtime
                    ? frame_runtime->resolver().mount_root().string()
                    : std::string{},
                .font_engine = frame_runtime ? &frame_runtime->font_engine() : nullptr,
                .runtime = frame_runtime,
            });
            dynamic_scene = evaluate_scene(ctx);
            scene_ptr = &dynamic_scene;
        }
        evaluate_ms = profiling::duration_ms(t_eval0, profiling::now());
        // Timeline/animation evaluation boundary (the pre-graph half of the
        // frame).  Accumulated per-frame so the render loop can delta it for
        // the per-frame architectural breakdown.
        if (sw_sidecar && sw_sidecar->counters()) {
            const auto eval_us = static_cast<uint64_t>(std::llround(evaluate_ms * 1000.0));
            sw_sidecar->counters()->timeline_eval_wall_ms.fetch_add(
                static_cast<uint64_t>(std::llround(evaluate_ms)),
                std::memory_order_relaxed);
            sw_sidecar->counters()->timeline_eval_wall_us.fetch_add(
                eval_us,
                std::memory_order_relaxed);
            sw_sidecar->counters()->timeline_patch_wall_us.fetch_add(
                eval_us,
                std::memory_order_relaxed);
        }
        layer_count = static_cast<int>(scene_ptr->layers().size());

        const auto t_scene0 = profiling::now();
        {
            CHRONON_TRACE_SCOPE("chronon.graph", "render_scene_graph");
            render_fb = call_graph(*scene_ptr, frame, 0.0f);
        }
        scene_ms = profiling::duration_ms(t_scene0, profiling::now());
    } else {
        const int N = std::max(2, settings.motion_blur.samples);

        chronon3d::temporal::TemporalSampleParams mb_params;
        mb_params.shutter_angle_deg = settings.motion_blur.shutter_angle_deg;
        mb_params.shutter_phase_deg = settings.motion_blur.shutter_phase_deg;
        mb_params.pattern           = settings.motion_blur.pattern;
        mb_params.filter            = settings.motion_blur.filter;
        mb_params.jitter_seed       = settings.motion_blur.jitter_seed;

        const chronon3d::temporal::TemporalSamplePlan sample_plan =
            chronon3d::temporal::make_temporal_sample_plan(
                mb_params, N, frame, spec.frame_rate,
                static_cast<std::size_t>(rw), static_cast<std::size_t>(rh),
                /*cache_version=*/0,
                chronon3d::temporal::TemporalBudgetResolver{
                    settings.render_budget.max_temporal_pixels});
        if (!sample_plan.valid()) {
            throw std::invalid_argument("motion-blur temporal sample plan is invalid");
        }

        // Every temporal sample owns all mutable frame domains.  In
        // particular, do not reuse the main NodeCache, CompiledGraphCache,
        // RenderSession, or transform scratch: those domains belong to the
        // integer-frame render and its history.  Sample contexts are kept
        // alive for the complete accumulation window and are never published
        // through commit_frame_state().
        std::vector<std::unique_ptr<cache::NodeCache>> sample_value_caches;
        std::vector<std::unique_ptr<CompiledGraphCache>> sample_topology_caches;
        std::vector<std::unique_ptr<RenderSession>> sample_sessions;
        std::vector<std::unique_ptr<TransformScratchBuffer>> sample_scratches;
        std::vector<std::shared_ptr<cache::FramebufferPool>> sample_framebuffer_pools;
        sample_value_caches.reserve(sample_plan.contexts.size());
        sample_topology_caches.reserve(sample_plan.contexts.size());
        sample_sessions.reserve(sample_plan.contexts.size());
        sample_scratches.reserve(sample_plan.contexts.size());
        sample_framebuffer_pools.reserve(sample_plan.contexts.size());
        const auto sample_pool_limit = frame_runtime
            ? (frame_runtime->config().cache().fb_pool_budget_bytes() > 0
                ? frame_runtime->config().cache().fb_pool_budget_bytes()
                : frame_runtime->config().cache().fb_pool_max_bytes())
            : cache::FramebufferPool::kDefaultBudgetBytes;
        auto* owning_memory_tracker = sw_sidecar
            ? sw_sidecar->session().memory_tracker.get()
            : nullptr;
        auto merge_sample_reports = [&](std::size_t completed_samples) {
            if (!owning_memory_tracker) return;
            for (std::size_t i = 0; i < completed_samples; ++i) {
                owning_memory_tracker->merge_report(
                    sample_sessions[i]->memory_tracker->snapshot());
            }
        };
        for (std::size_t i = 0; i < sample_plan.contexts.size(); ++i) {
            auto sample_cache = std::make_unique<cache::NodeCache>();
            if (frame_runtime) {
                sample_cache->set_capacity(
                    frame_runtime->config().cache().node_cache_max_bytes());
            }
            sample_value_caches.push_back(std::move(sample_cache));
            sample_topology_caches.push_back(std::make_unique<CompiledGraphCache>());
            sample_sessions.push_back(std::make_unique<RenderSession>());
            sample_scratches.push_back(std::make_unique<TransformScratchBuffer>());
            sample_framebuffer_pools.push_back(
                cache::FramebufferPool::create_shared(sample_pool_limit));
        }

        render_fb = sample_framebuffer_pools.front()->acquire(
            rw, rh, /*clear=*/true);

        float actual_weight_sum = 0.0f;

        const auto t_mb0 = profiling::now();
        {
            CHRONON_TRACE_SCOPE("chronon.effect", "motion_blur_accumulation");
            for (int s = 0; s < sample_plan.num_samples(); ++s) {
                if (cancellation && cancellation->is_cancelled()) {
                    merge_sample_reports(static_cast<std::size_t>(s));
                    return nullptr;
                }
                const auto& sample = sample_plan[static_cast<std::size_t>(s)];
                const float t = static_cast<float>(sample.time.frame - static_cast<double>(frame));
                const float w = sample.weight;
                actual_weight_sum += w;
                const FrameContext sub_ctx = make_frame_context({
                    .global_time = sample.time,
                    .duration = spec.duration,
                    .width = spec.width,
                    .height = spec.height,
                    .assets_root = frame_runtime
                        ? frame_runtime->resolver().mount_root().string()
                        : std::string{},
                    .font_engine = frame_runtime ? &frame_runtime->font_engine() : nullptr,
                    .runtime = frame_runtime,
                });
                Scene sub = evaluate_scene(sub_ctx);
                if (s == 0) layer_count = static_cast<int>(sub.layers().size());
                const TemporalRenderContext sample_context{
                    .sample_key = sample.cache_key,
                    .sample_time = sample.time,
                    .value_cache = sample_value_caches[static_cast<std::size_t>(s)].get(),
                    .topology_cache = sample_topology_caches[static_cast<std::size_t>(s)].get(),
                    .session = sample_sessions[static_cast<std::size_t>(s)].get(),
                    .scratch = sample_scratches[static_cast<std::size_t>(s)].get(),
                    .counters = nullptr,
                    .framebuffer_pool = sample_framebuffer_pools[static_cast<std::size_t>(s)],
                };
                auto sample_fb = call_graph(sub, frame, t, &sample_context);
                if (!sample_fb) {
                    merge_sample_reports(static_cast<std::size_t>(s + 1));
                    return nullptr;
                }
                const Framebuffer& sub_fb = *sample_fb;

                if (cancellation && cancellation->is_cancelled()) {
                    merge_sample_reports(static_cast<std::size_t>(s + 1));
                    return nullptr;
                }
                tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
                    [&](const tbb::blocked_range<int>& range) {
                        using namespace hwy::HWY_NAMESPACE;
                        const ScalableTag<float> df;
                        const size_t lanes = Lanes(df);
                        const auto v_weight = Set(df, w);

                        for (int y = range.begin(); y < range.end(); ++y) {
                            const float* src = reinterpret_cast<const float*>(sub_fb.pixels_row(y));
                            float* dst = reinterpret_cast<float*>(render_fb->pixels_row(y));
                            const int total_floats = rw * 4;

                            int x = 0;
                            for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                                auto acc = LoadU(df, dst + x);
                                auto vals = LoadU(df, src + x);
                                acc = MulAdd(vals, v_weight, acc);
                                StoreU(acc, df, dst + x);
                            }
                            for (; x < total_floats; ++x) {
                                dst[x] += src[x] * w;
                            }
                        }
                    }
                );
            }
        }
        merge_sample_reports(sample_plan.contexts.size());
        motion_blur_ms = profiling::duration_ms(t_mb0, profiling::now());

        const float post_norm = (actual_weight_sum > 1e-6f)
            ? (1.0f / actual_weight_sum)
            : 1.0f;

        {
            CHRONON_TRACE_SCOPE("chronon.effect", "motion_blur_normalize_in_place");
            tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
                [&](const tbb::blocked_range<int>& range) {
                    using namespace hwy::HWY_NAMESPACE;
                    const ScalableTag<float> df;
                    const size_t lanes = Lanes(df);
                    const auto v_post = Set(df, post_norm);

                    for (int y = range.begin(); y < range.end(); ++y) {
                        float* row = reinterpret_cast<float*>(render_fb->pixels_row(y));
                        const int total_floats = rw * 4;

                        int x = 0;
                        for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                            auto v = LoadU(df, row + x);
                            v = Mul(v, v_post);
                            StoreU(v, df, row + x);
                        }
                        for (; x < total_floats; ++x) {
                            row[x] *= post_norm;
                        }
                    }
                }
            );
        }
    }

    if (sw_sidecar && !want_temporal_accumulation) {
        sw_sidecar->dirty_telemetry().last_layer_count = layer_count;
    }

    if (ssaa > 1.0f) {
        const auto t_down0 = profiling::now();
        std::unique_ptr<Framebuffer> out;
        {
            CHRONON_TRACE_SCOPE("chronon.effect", "downsample_ssaa");
            out = downsample_fb(*render_fb, w, h);
        }
        return std::shared_ptr<Framebuffer>(out.release());
    }

    return render_fb;
}

std::shared_ptr<Framebuffer> render_compiled_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    const CompiledComposition& compiled,
    Frame frame,
    chronon3d::SoftwareRenderer* sw_sidecar)
{
    return render_compiled_composition_frame_temporal(
        backend, node_cache, settings, registry, video_decoder, compiled,
        frame, sw_sidecar, nullptr);
}

} // namespace chronon3d::graph
