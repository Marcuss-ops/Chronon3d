#include "render_job_finalize.hpp"
#include "../telemetry/telemetry_run.hpp"
#include "report/render_job_report.hpp"

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <spdlog/spdlog.h>

#include <filesystem>

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

} // anonymous namespace

bool finalize_render_job(
    const RenderJobPlan& plan,
    RenderJobSetupResult& setup,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double total_render_ms,
    double total_encode_ms,
    int frames_written,
    bool ok,
    profiling::Clock::time_point loop_t0,
    profiling::Clock::time_point loop_t1)
{
    spdlog::info("Render complete.");

    // ── Cache diagnostics snapshot ──────────────────────────────────
    spdlog::info("\n{}", chronon3d::cache::format_cache_snapshot());

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(setup.wall_t0, wall_t1);
    if (setup.renderer->counters()) {
        setup.sys_metrics.fill_system_counters(*setup.renderer->counters());
    }
    const auto* counters = setup.renderer->counters();

    // ── Build the top-level run record ────────────────────────────────
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
    run.effective_fps = wall_time_ms > 0.0 ? (run.frames_total * 1000.0 / wall_time_ms) : 0.0;
    run.started_at_iso = setup.job_started_iso;
    run.finished_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();

    if (counters) {
        cli::telemetry::populate_run_metrics(run, *counters);
    }

    // ── Pool metrics ─────────────────────────────────────────────────
    const auto pool_current_bytes = setup.renderer->software_framebuffer_pool().current_bytes();
    const auto pool_available_count = setup.renderer->software_framebuffer_pool().available_count();

    // ── Counter snapshot ──────────────────────────────────────────────
    std::vector<chronon3d::telemetry::CounterTelemetryRecord> counters_list;
    if (counters) {
        counters_list = cli::telemetry::capture_counters(*counters);
    }
    counters_list.push_back({"pool_current_bytes", pool_current_bytes});
    counters_list.push_back({"pool_available_count", pool_available_count});

    // ── Phase timing records ──────────────────────────────────────────
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases = {
        {"setup_renderer", profiling::duration_ms(setup.setup_t0, setup.setup_t1)}
    };
    if (setup.renderer->counters()) {
        auto graph_phases = cli::telemetry::capture_graph_phase_records(*setup.renderer->counters());
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }
    phases.push_back({"rendering_loop", profiling::duration_ms(loop_t0, loop_t1)});

    // ── Flush per-node telemetry collected during graph execution ─────
    auto telemetry = chronon3d::telemetry::collect_all_telemetry();

    // ── Write telemetry (only when --report is set) ───────────────────
    const bool write_telemetry = plan.report;
    if (write_telemetry) {
        chronon3d::telemetry::TelemetryManager::instance().record_run(
            run, telemetry_frames, phases, counters_list,
            telemetry.node_events, telemetry.layer_events,
            telemetry.cache_events, telemetry.culling_events,
            telemetry.text_events, telemetry.image_events,
            telemetry.tile_events);
    }

    // ── Generate execution report ─────────────────────────────────────
    if (plan.report) {
        RenderReportContext ctx;
        ctx.run                = run;
        ctx.counters           = counters_list;
        ctx.phases             = phases;
        ctx.frames             = telemetry_frames;
        ctx.pool_current_bytes = pool_current_bytes;
        ctx.pool_available_count = pool_available_count;
        ctx.command_line       = plan.command_line;
        generate_execution_report(ctx);
    }

    return ok;
}

} // namespace chronon3d::cli
