#include "render_job_detail.hpp"
#include "report/render_job_report.hpp"
#include "../telemetry/telemetry_run.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <vector>

namespace chronon3d::cli {
    extern std::string g_command_line;
}

namespace chronon3d::cli {

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
    bool ok = true;

    // Capture CPU baseline before the render loop — fill_system_counters()
    // uses sample_cpu_delta() to compute per-run CPU time.
    sys_metrics.sample_cpu_start();

    const auto loop_t0 = std::chrono::steady_clock::now();
    for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
        if (!write_render_frame(*plan.comp, *renderer, static_cast<Frame>(f), plan.range, plan.output, ok,
                                telemetry_frames, total_render_ms, total_encode_ms, frames_written)) {
            // keep going to report all failures, but preserve false
        }
    }
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
    run.output_path = plan.output;
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
        ctx.counters          = counters_list;
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
