#include "composition_frame_internal.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <chrono>
#include <cmath>
#include <utility>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

bool CompositionFrameState::render_single_frame() {
if (cancellation && cancellation->is_cancelled()) {
    return false;
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
if (compiled.template_scene && compiled.is_static_topology) {
    scene_ptr = compiled.template_scene.get();
} else {
    CHRONON_TRACE_SCOPE("chronon.frame", "evaluate_composition");
    const FrameContext ctx = make_frame_context({
        .global_time = SampleTime::from_frame(static_cast<double>(frame), spec.frame_rate),
        .duration = spec.duration,
        .width = spec.width,
        .height = spec.height,
        .assets_root = frame_assets_root,
        .font_engine = frame_runtime ? &frame_runtime->font_engine() : nullptr,
        .runtime = frame_runtime,
    });
    dynamic_scene = evaluate_scene(ctx);
    scene_ptr = &dynamic_scene;
}
const double evaluate_ms = profiling::duration_ms(t_eval0, profiling::now());
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
[[maybe_unused]] const double scene_ms = profiling::duration_ms(t_scene0, profiling::now());
return true;
}

}  // namespace chronon3d::graph::detail
