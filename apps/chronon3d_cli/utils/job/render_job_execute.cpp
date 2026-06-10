#include "render_job_detail.hpp"
#include "report/render_job_report.hpp"
#include "../telemetry/telemetry_run.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include "write_pool.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <future>
#include <vector>

namespace chronon3d::cli {
    extern std::string g_command_line;
}

namespace chronon3d::cli {

namespace {
std::string resolve_output_path_for_telemetry(const std::string& output) {
    if (output.empty()) {
        return output;
    }

    std::filesystem::path resolved(output);
    if (!resolved.is_absolute()) {
        resolved = std::filesystem::absolute(resolved);
    }
    return resolved.lexically_normal().string();
}
}

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan) {
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    const auto job_started_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();
    SystemMetricsCollector sys_metrics;

    const auto setup_t0 = std::chrono::steady_clock::now();
    auto renderer = create_renderer(registry, plan.settings);
    const auto renderer_t1 = std::chrono::steady_clock::now();
    if (renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(renderer_t1 - setup_t0).count());
        renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    // Renderer warmup (preallocate framebuffers + optional dummy frame)
    uint64_t saved_fb_alloc = 0;
    uint64_t saved_fb_reuses = 0;
    uint64_t saved_fb_bytes = 0;
    uint64_t saved_fb_peak = 0;
    if (plan.warmup_renderer) {
        const auto warmup_t0 = std::chrono::steady_clock::now();
        runtime::warmup_renderer(*renderer, *plan.comp, runtime::RendererWarmupOptions{
            .width = plan.comp->width(),
            .height = plan.comp->height(),
            .framebuffer_count = plan.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = plan.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = (plan.log_level != "trace" && plan.log_level != "debug")
        });
        const auto warmup_t1 = std::chrono::steady_clock::now();

        if (renderer->counters()) {
            const auto warmup_ms = static_cast<uint64_t>(
                std::chrono::duration<double, std::milli>(warmup_t1 - warmup_t0).count());
            renderer->counters()->setup_pool_preallocation_ms.fetch_add(warmup_ms, std::memory_order_relaxed);

            saved_fb_alloc = renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
            saved_fb_reuses = renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
            saved_fb_bytes = renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
            saved_fb_peak = renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
        }
    }

    renderer->counters()->reset();

    if (renderer->counters()) {
        renderer->counters()->framebuffer_allocations.store(saved_fb_alloc, std::memory_order_relaxed);
        renderer->counters()->framebuffer_reuses.store(saved_fb_reuses, std::memory_order_relaxed);
        renderer->counters()->framebuffer_bytes_allocated.store(saved_fb_bytes, std::memory_order_relaxed);
        renderer->counters()->framebuffer_bytes_peak.store(saved_fb_peak, std::memory_order_relaxed);
    }

    // Clear per-event telemetry stores after warmup, since atomic counters
    // were reset above.  This keeps Hot Nodes events in sync with
    // nodes_executed/composite_calls atomic counters.
    chronon3d::telemetry::clear_telemetry_stores();

    const auto setup_t1 = std::chrono::steady_clock::now();

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

    const bool write_telemetry = plan.report;
    if (write_telemetry) {
        // Initialize the default telemetry manager stores only when we are
        // actually generating an execution report. Normal renders should not
        // depend on SQLite telemetry being available.
        chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();
    }

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    double total_render_ms = 0.0;
    double total_encode_ms = 0.0;
    int frames_written = 0;

    const int64_t effective_end = (plan.range.start == plan.range.end) ? plan.range.start + 1 : plan.range.end;
    const int64_t total_frames = (effective_end - plan.range.start + plan.range.step - 1) / plan.range.step;
    bool ok = true;

    // Capture CPU baseline before the render loop
    sys_metrics.sample_cpu_start();

    const auto loop_t0 = std::chrono::steady_clock::now();

    // ── Double-buffered render/write pipeline ────────────────────────
    // Overlaps CPU-bound rendering of frame N+1 with I/O-bound writing
    // of frame N.  For single-frame renders, falls back to sequential.
    //
    // Pipeline:
    //   Frame 0: render(0) → launch async_write(0)
    //   Frame N: render(N) → wait_write(N-1) → launch async_write(N)
    //   Last:    wait_write(last)
    if (total_frames > 1) {
        // ── Bounded write pool ────────────────────────────────
        // Up to 2 writes can be in flight at the same time: the current
        // frame's write + the next frame's write (if the render loop is
        // already past it).  This is a small but real win over the old
        // std::async chain that only allowed 1 in-flight write (because
        // the previous write had to complete before the next was
        // launched).  The pool also reuses 2 worker threads for the
        // entire render instead of spawning one detached thread per
        // frame (saves ~900 thread spawns on a 900-frame clip).
        WritePool write_pool(2);

        // ── Frame 0: render + submit write to pool ─────────────────────
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

            // ── Pipeline: render frame N, wait for write of N-1, submit write of N
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

                // Wait for previous frame's write to finish (propagates exceptions).
                // This also serves as a backpressure point: if the pool is full,
                // this .get() blocks until the previous write completes.
                prev_write.get();

                // Submit write for current frame to the pool (overlap with next render)
                prev_write = write_pool.submit(
                    WritePool::Job([fb_n, f, &plan, cache_hit_n, dirty_ratio_n, render_ms_n,
                                    &ok, &telemetry_frames, &total_encode_ms, &frames_written] {
                        return write_frame_to_disk(
                            fb_n, static_cast<Frame>(f), plan.range, plan.output,
                            cache_hit_n, dirty_ratio_n, render_ms_n,
                            ok, telemetry_frames, total_encode_ms, frames_written);
                    }));
            }

            // Wait for the final async write (propagates exceptions)
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

    spdlog::info("Render complete.");



    const auto wall_t1 = std::chrono::steady_clock::now();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();
    if (renderer->counters()) {
        sys_metrics.fill_system_counters(*renderer->counters());
    }
    const auto* counters = renderer->counters();
    chronon3d::telemetry::RenderTelemetryRecord run;
    run.run_id = chronon3d::telemetry::TelemetryManager::generate_uuid();
    run.composition_id = plan.comp_id;
    run.output_path = resolve_output_path_for_telemetry(plan.output);
    run.success = ok;
    run.frames_total = static_cast<int>(telemetry_frames.size());
    run.frames_written = frames_written;
    run.wall_time_ms = wall_time_ms;
    run.render_ms = total_render_ms;
    run.encode_ms = total_encode_ms;
    run.effective_fps = wall_time_ms > 0 ? (run.frames_total * 1000.0 / wall_time_ms) : 0.0;
    run.started_at_iso = job_started_iso;
    run.finished_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();

    if (counters) {
        cli::telemetry::populate_run_metrics(run, *counters);
    }

    // Pool metrics
    const auto pool_current_bytes = renderer->software_framebuffer_pool().current_bytes();
    const auto pool_available_count = renderer->software_framebuffer_pool().available_count();

    std::vector<chronon3d::telemetry::CounterTelemetryRecord> counters_list;
    if (counters) {
        counters_list = cli::telemetry::capture_counters(*counters);
    }
    counters_list.push_back({"pool_current_bytes", pool_current_bytes});
    counters_list.push_back({"pool_available_count", pool_available_count});

    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases = {
        {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()}
    };
    if (renderer->counters()) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*renderer->counters());
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }
    phases.push_back({"rendering_loop", std::chrono::duration<double, std::milli>(loop_t1 - loop_t0).count()});

    // Flush per-node telemetry collected during graph execution
    auto telemetry = chronon3d::telemetry::collect_all_telemetry();

    if (write_telemetry) {
        chronon3d::telemetry::TelemetryManager::instance().record_run(run, telemetry_frames, phases, counters_list,
                                                            telemetry.node_events, telemetry.layer_events,
                                                            telemetry.cache_events, telemetry.culling_events,
                                                            telemetry.text_events, telemetry.image_events,
                                                            telemetry.tile_events);
    }

    if (plan.report) {
        RenderReportContext ctx;
        ctx.run               = run;
        ctx.telemetry.counters          = counters_list;
        ctx.phases            = phases;
        ctx.frames            = telemetry_frames;
        ctx.pool_current_bytes = pool_current_bytes;
        ctx.pool_available_count = pool_available_count;
        ctx.command_line      = cli::g_command_line;
        generate_execution_report(ctx);
    }

    return ok;
}

} // namespace chronon3d::cli
