#include "render_job_loop.hpp"
#include "render_job_detail.hpp"
#include "write_pool.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <future>

namespace chronon3d::cli {

RenderLoopResult run_render_job_loop(
    const RenderJobPlan& plan,
    SoftwareRenderer& renderer)
{
    RenderLoopResult result;

    const bool write_telemetry = plan.report;
    if (write_telemetry) {
        // Initialise the default telemetry manager stores only when we are
        // actually generating an execution report. Normal renders should not
        // depend on SQLite telemetry being available.
        chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();
    }

    const int64_t effective_end = (plan.range.start == plan.range.end)
                                      ? plan.range.start + 1
                                      : plan.range.end;
    const int64_t total_frames = (effective_end - plan.range.start + plan.range.step - 1) / plan.range.step;

    result.loop_start = profiling::now();

    // ── Double-buffered render / write pipeline ────────────────────────
    // Overlaps CPU-bound rendering of frame N+1 with I/O-bound writing
    // of frame N.  For single-frame renders, falls back to sequential.
    //
    // Pipeline:
    //   Frame 0: render(0) → launch async_write(0)
    //   Frame N: render(N) → wait_write(N-1) → launch async_write(N)
    //   Last:    wait_write(last)
    if (total_frames > 1) {
        // ── Bounded write pool ──────────────────────────────────
        // Up to 2 writes can be in flight at the same time: the current
        // frame's write + the next frame's write (if the render loop is
        // already past it).  The pool reuses 2 worker threads for the
        // entire render instead of spawning one detached thread per
        // frame.
        WritePool write_pool(2);

        // ── Frame 0: render + submit write to pool ─────────────
        int64_t f = plan.range.start;
        {
            const auto hits_before = renderer.node_cache().stats().hits;
            const auto t0 = profiling::now();
            auto fb = renderer.render_frame(*plan.comp, static_cast<Frame>(f));
            const auto t1 = profiling::now();
            const auto hits_after = renderer.node_cache().stats().hits;

            if (!fb) {
                spdlog::error("Failed to render frame {}", f);
                result.ok = false;
                goto render_loop_done;
            }

            const bool cache_hit = (hits_after > hits_before);
            const double render_ms = profiling::duration_ms(t0, t1);
            const double dirty_ratio = renderer.last_dirty_area_ratio();
            result.total_render_ms += render_ms;

            int prog_cache_cap_0 = static_cast<int>(
                renderer.counters()
                    ? renderer.counters()->program_cache_capacity.load(std::memory_order_relaxed)
                    : 0);

            // Submit write for frame 0 to the pool
            std::future<double> prev_write = write_pool.submit(
                WritePool::Job([fb, f, &plan, cache_hit, dirty_ratio, render_ms,
                                prog_cache_cap_0, &result] {
                    return write_frame_to_disk(
                        fb, static_cast<Frame>(f), plan.range, plan.output,
                        cache_hit, dirty_ratio, render_ms, prog_cache_cap_0,
                        result.ok, result.telemetry_frames, result.total_encode_ms, result.frames_written);
                }));

            f += plan.range.step;

            // ── Pipeline: render N → wait N-1 → submit N ──────
            for (; f < effective_end; f += plan.range.step) {
                const auto hits_before_n = renderer.node_cache().stats().hits;
                const auto t0_n = profiling::now();
                auto fb_n = renderer.render_frame(*plan.comp, static_cast<Frame>(f));
                const auto t1_n = profiling::now();
                const auto hits_after_n = renderer.node_cache().stats().hits;

                if (!fb_n) {
                    spdlog::error("Failed to render frame {}", f);
                    result.ok = false;
                    prev_write.get();
                    write_pool.drain();
                    goto render_loop_done;
                }

                const bool cache_hit_n = (hits_after_n > hits_before_n);
                const double render_ms_n = profiling::duration_ms(t0_n, t1_n);
                const double dirty_ratio_n = renderer.last_dirty_area_ratio();
                result.total_render_ms += render_ms_n;

                int prog_cache_cap_n = static_cast<int>(
                    renderer.counters()
                        ? renderer.counters()->program_cache_capacity.load(std::memory_order_relaxed)
                        : 0);

                // Wait for previous frame's write to finish.
                prev_write.get();

                // Submit write for current frame (overlap with next render)
                prev_write = write_pool.submit(
                    WritePool::Job([fb_n, f, &plan, cache_hit_n, dirty_ratio_n, render_ms_n,
                                    prog_cache_cap_n, &result] {
                        return write_frame_to_disk(
                            fb_n, static_cast<Frame>(f), plan.range, plan.output,
                            cache_hit_n, dirty_ratio_n, render_ms_n, prog_cache_cap_n,
                            result.ok, result.telemetry_frames, result.total_encode_ms, result.frames_written);
                    }));
            }

            // Wait for the final async write
            prev_write.get();
            write_pool.drain();
        }
    } else {
        // ── Single-frame: sequential fallback ─────────────────────────
        for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
            if (!write_render_frame(*plan.comp, renderer, static_cast<Frame>(f), plan.range, plan.output,
                                    result.ok, result.telemetry_frames, result.total_render_ms,
                                    result.total_encode_ms, result.frames_written)) {
                // keep going to report all failures, but preserve false
            }
        }
    }

render_loop_done:
    result.loop_end = profiling::now();

    return result;
}

} // namespace chronon3d::cli
