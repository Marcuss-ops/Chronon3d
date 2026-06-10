#include "render_job_detail.hpp"
#include "render_job_setup.hpp"
#include "render_job_finalize.hpp"
#include "write_pool.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <future>

namespace chronon3d::cli {

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan) {
    // ═══════════════════════════════════════════════════════════════════
    // PHASE 1 — Setup:  asset mount, renderer creation, warmup,
    //                    counter reset, telemetry-store clear
    // ═══════════════════════════════════════════════════════════════════
    RenderJobSetupResult setup;
    setup_render_job(registry, plan, setup);
    if (!setup.renderer) {
        spdlog::error("Failed to create renderer for composition '{}'", plan.comp_id);
        return false;
    }

    auto& renderer = setup.renderer;

    // ── Short interlude: specscene motion-blur warning ──────────────
    if (plan.from_specscene && plan.settings.motion_blur.enabled) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                 plan.comp_id, plan.range.start, plan.range.end, plan.range.step,
                 plan.settings.motion_blur.enabled
                     ? fmt::format(" [MB {}smp {:.0f}°]",
                                   plan.settings.motion_blur.samples,
                                   plan.settings.motion_blur.shutter_angle)
                     : "",
                 plan.settings.ssaa_factor > 1.0f
                     ? fmt::format(" [SSAA {:.1f}x]", plan.settings.ssaa_factor)
                     : "");

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2 — Render Loop:  double-buffered or single-frame fallback
    // ═══════════════════════════════════════════════════════════════════
    const bool write_telemetry = plan.report;
    if (write_telemetry) {
        // Initialise the default telemetry manager stores only when we are
        // actually generating an execution report. Normal renders should not
        // depend on SQLite telemetry being available.
        chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();
    }

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    double total_render_ms = 0.0;
    double total_encode_ms = 0.0;
    int frames_written = 0;

    const int64_t effective_end = (plan.range.start == plan.range.end)
                                      ? plan.range.start + 1
                                      : plan.range.end;
    const int64_t total_frames = (effective_end - plan.range.start + plan.range.step - 1) / plan.range.step;
    bool ok = true;

    // Capture CPU baseline before the render loop
    setup.sys_metrics.sample_cpu_start();

    const auto loop_t0 = std::chrono::steady_clock::now();

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
            const auto hits_before = renderer->node_cache().stats().hits;
            const auto t0 = std::chrono::steady_clock::now();
            auto fb = renderer->render_frame(*plan.comp, static_cast<Frame>(f));
            const auto t1 = std::chrono::steady_clock::now();
            const auto hits_after = renderer->node_cache().stats().hits;

            if (!fb) {
                spdlog::error("Failed to render frame {}", f);
                ok = false;
                goto render_loop_done;
            }

            const bool cache_hit = (hits_after > hits_before);
            const double render_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double dirty_ratio = renderer->last_dirty_area_ratio();
            total_render_ms += render_ms;

            // Submit write for frame 0 to the pool
            std::future<double> prev_write = write_pool.submit(
                WritePool::Job([fb, f, &plan, cache_hit, dirty_ratio, render_ms,
                                &ok, &telemetry_frames, &total_encode_ms, &frames_written] {
                    return write_frame_to_disk(
                        fb, static_cast<Frame>(f), plan.range, plan.output,
                        cache_hit, dirty_ratio, render_ms,
                        ok, telemetry_frames, total_encode_ms, frames_written);
                }));

            f += plan.range.step;

            // ── Pipeline: render N → wait N-1 → submit N ──────
            for (; f < effective_end; f += plan.range.step) {
                const auto hits_before_n = renderer->node_cache().stats().hits;
                const auto t0_n = std::chrono::steady_clock::now();
                auto fb_n = renderer->render_frame(*plan.comp, static_cast<Frame>(f));
                const auto t1_n = std::chrono::steady_clock::now();
                const auto hits_after_n = renderer->node_cache().stats().hits;

                if (!fb_n) {
                    spdlog::error("Failed to render frame {}", f);
                    ok = false;
                    prev_write.get();
                    write_pool.drain();
                    goto render_loop_done;
                }

                const bool cache_hit_n = (hits_after_n > hits_before_n);
                const double render_ms_n = std::chrono::duration<double, std::milli>(t1_n - t0_n).count();
                const double dirty_ratio_n = renderer->last_dirty_area_ratio();
                total_render_ms += render_ms_n;

                // Wait for previous frame's write to finish.
                prev_write.get();

                // Submit write for current frame (overlap with next render)
                prev_write = write_pool.submit(
                    WritePool::Job([fb_n, f, &plan, cache_hit_n, dirty_ratio_n, render_ms_n,
                                    &ok, &telemetry_frames, &total_encode_ms, &frames_written] {
                        return write_frame_to_disk(
                            fb_n, static_cast<Frame>(f), plan.range, plan.output,
                            cache_hit_n, dirty_ratio_n, render_ms_n,
                            ok, telemetry_frames, total_encode_ms, frames_written);
                    }));
            }

            // Wait for the final async write
            prev_write.get();
            write_pool.drain();
        }
    } else {
        // ── Single-frame: sequential fallback ─────────────────────────
        for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
            if (!write_render_frame(*plan.comp, *renderer, static_cast<Frame>(f), plan.range, plan.output, ok,
                                    telemetry_frames, total_render_ms, total_encode_ms, frames_written)) {
                // keep going to report all failures, but preserve false
            }
        }
    }

render_loop_done:
    const auto loop_t1 = std::chrono::steady_clock::now();

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 3 — Finalize:  telemetry collection, report generation
    // ═══════════════════════════════════════════════════════════════════
    return finalize_render_job(plan, setup, telemetry_frames,
                               total_render_ms, total_encode_ms, frames_written,
                               ok, loop_t0, loop_t1);
}

} // namespace chronon3d::cli
