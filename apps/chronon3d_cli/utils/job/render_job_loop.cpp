#include "render_job_loop.hpp"
#include "render_job_detail.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace chronon3d::cli {

RenderLoopResult run_render_job_loop(
    const RenderJobPlan& plan,
    SoftwareRenderer & renderer)
{
    RenderLoopResult result;

    const int64_t effective_end = (plan.range.start == plan.range.end)
                                      ? plan.range.start + 1
                                      : plan.range.end;
    const int64_t total_frames = (effective_end - plan.range.start + plan.range.step - 1) / plan.range.step;

    result.loop_start = profiling::now();

    // ── Sequential render / write loop ─────────────────────────────────
    // The previous WritePool-based double buffering has been removed in
    // favour of the unified CpuBudget.  Encode work is now accounted for
    // in the encode_threads budget and runs synchronously in the render
    // thread, eliminating the nested std::thread pool.
    for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
        if (!write_render_frame(*plan.comp, renderer, static_cast<Frame>(f), plan.range, plan.output,
                                result.ok, result.telemetry_frames, result.total_render_ms,
                                result.total_encode_ms, result.frames_written)) {
            // keep going to report all failures, but preserve false
        }
    }

    result.loop_end = profiling::now();

    return result;
}

} // namespace chronon3d::cli
