#include "render_job_finalize.hpp"
#include "../telemetry/telemetry_run.hpp"
#include "report/render_job_report.hpp"

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// ── Direct JSONL write (bypasses TelemetryManager when SQLite not compiled) ───
void write_run_to_jsonl(const chronon3d::telemetry::RenderTelemetryRecord& run) {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::filesystem::path jsonl_path = std::filesystem::path(home) /
        ".chronon3d" / "telemetry" / "render_history.jsonl";

    std::error_code ec;
    std::filesystem::create_directories(jsonl_path.parent_path(), ec);

    // Escape helper: escape backslashes and double quotes for JSON string values.
    auto json_escape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;   // → \"
            case '\\': out += "\\\\"; break;   // → \\
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;       break;
            }
        }
        return out;
    };

    std::ostringstream js;
    js << "{";
    js << "\"type\":\"run\"";
    js << ",\"run_id\":\"" << json_escape(run.run_id) << "\"";
    js << ",\"composition_id\":\"" << json_escape(run.composition_id) << "\"";
    js << ",\"output_path\":\"" << json_escape(run.output_path) << "\"";
    js << ",\"success\":" << (run.success ? "1" : "0");
    js << ",\"frames_total\":" << run.frames_total;
    js << ",\"frames_written\":" << run.frames_written;
    js << ",\"wall_time_ms\":" << run.wall_time_ms;
    js << ",\"render_ms\":" << run.render_ms;
    js << ",\"encode_ms\":" << run.encode_ms;
    js << ",\"effective_fps\":" << run.effective_fps;
    js << ",\"started_at_iso\":\"" << json_escape(run.started_at_iso) << "\"";
    js << ",\"finished_at_iso\":\"" << json_escape(run.finished_at_iso) << "\"";
    js << ",\"git_commit_short\":\"" << json_escape(run.git_commit_short) << "\"";
    js << ",\"build_type\":\"" << json_escape(run.build_type) << "\"";
    js << ",\"os\":\"" << json_escape(run.os) << "\"";
    js << ",\"cpu_model\":\"" << json_escape(run.cpu_model) << "\"";
    js << ",\"cores\":" << run.cores;
    js << ",\"cache_hits\":" << run.cache_hits;
    js << ",\"cache_misses\":" << run.cache_misses;
    js << ",\"pixels_touched\":" << run.pixels_touched;
    js << ",\"dirty_pixels\":" << run.dirty_pixels;
    js << ",\"framebuffer_allocations\":" << run.framebuffer_allocations;
    js << ",\"framebuffer_reuses\":" << run.framebuffer_reuses;
    js << ",\"framebuffer_bytes_allocated\":" << run.framebuffer_bytes_allocated;
    js << ",\"framebuffer_bytes_peak\":" << run.framebuffer_bytes_peak;
    js << ",\"compiler_info\":\"" << json_escape(run.compiler_info) << "\"";
    js << "}\n";

    std::ofstream out(jsonl_path, std::ios::app);
    if (out.is_open()) {
        out << js.str();
        out.close();
        spdlog::info("[report] Telemetry run written to JSONL: {}", run.run_id);
    } else {
        spdlog::warn("[report] Failed to open JSONL for append: {}", jsonl_path.string());
    }
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
    // FIXME: format_cache_snapshot() segfaults when called after
    // TelemetryManager::initialize_default_stores() has opened the SQLite
    // database (only happens with --report).  Skip the snapshot in that
    // mode to avoid the crash; the cache stats are still available in
    // the telemetry counters written to the DB.
    if (!plan.report) {
        spdlog::info("\n{}", chronon3d::cache::format_cache_snapshot());
    }

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
        // Write directly to JSONL (dashboard reads from both SQLite + JSONL).
        // TelemetryManager is only used for UUID generation and timestamps;
        // the actual telemetry SQLite store requires a compile flag not
        // available in fast-dev builds.
        write_run_to_jsonl(run);
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
