#include "render_job_finalize.hpp"
#include "../process_start.hpp"
#include "../telemetry/telemetry_run.hpp"
#include "report/render_job_report.hpp"

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

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

void write_run_to_jsonl(const chronon3d::telemetry::RenderTelemetryRecord& run) {
    std::filesystem::path jsonl_path;
    const char* env_path = std::getenv("CHRONON3D_TELEMETRY_PATH");
    if (env_path && env_path[0] != '\0') {
        std::filesystem::path env_base(env_path);
        if (env_base.extension() == ".db" || env_base.extension() == ".sqlite") {
            jsonl_path = env_base.parent_path() / "render_history.jsonl";
        } else {
            jsonl_path = env_base / "render_history.jsonl";
        }
    } else {
        const char* home = std::getenv("HOME");
        if (!home) return;
        jsonl_path = std::filesystem::path(home) /
            ".chronon3d" / "telemetry" / "render_history.jsonl";
    }

    std::error_code ec;
    std::filesystem::create_directories(jsonl_path.parent_path(), ec);

    auto json_escape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
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
    js << ",\"bytes_allocated_peak\":" << run.bytes_allocated_peak;
    js << ",\"logical_resource_count\":" << run.logical_resource_count;
    js << ",\"physical_resource_slot_count\":" << run.physical_resource_slot_count;
    js << ",\"logical_resource_bytes\":" << run.logical_resource_bytes;
    js << ",\"physical_resource_bytes\":" << run.physical_resource_bytes;
    js << ",\"alias_saved_bytes\":" << run.alias_saved_bytes;
    js << ",\"alias_reuse_count\":" << run.alias_reuse_count;
    js << ",\"new_resource_slot_count\":" << run.new_resource_slot_count;
    js << ",\"arena_peak_bytes\":" << run.arena_peak_bytes;
    js << ",\"process_startup_ms\":" << run.process_startup_ms;
    js << ",\"framebuffer_allocations_per_frame\":" << run.framebuffer_allocations_per_frame;
    js << ",\"ffprobe_wall_ms\":" << run.ffprobe_wall_ms;
    js << ",\"sha256_wall_ms\":" << run.sha256_wall_ms;
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
    const RenderJob& job,
    RenderJobSetupResult& setup,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames,
    double total_render_ms,
    double total_encode_ms,
    int frames_written,
    bool ok,
    profiling::Clock::time_point loop_t0,
    profiling::Clock::time_point loop_t1)
{
    spdlog::info("Render complete.");

    if (!job.execution.report) {
        spdlog::info("\n{}", chronon3d::cache::format_cache_snapshot(
            setup.renderer->runtime().diagnostics()));
    }

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(setup.wall_t0, wall_t1);
    if (setup.renderer->counters()) {
        setup.sys_metrics.fill_system_counters(*setup.renderer->counters());
    }
    const auto* counters = setup.renderer->counters();

    chronon3d::telemetry::RenderTelemetryRecord run;
    run.run_id = chronon3d::telemetry::TelemetryManager::generate_uuid();
    run.composition_id = job.comp_id;
    run.output_path = resolve_output_path_for_telemetry(job.output);
    run.success = ok;
    run.frames_total = static_cast<int>(telemetry_frames.size());
    run.frames_written = frames_written;
    run.wall_time_ms = wall_time_ms;
    run.render_ms = total_render_ms;
    run.encode_ms = total_encode_ms;
    run.effective_fps = wall_time_ms > 0.0
        ? (static_cast<double>(run.frames_written) * 1000.0 / wall_time_ms)
        : 0.0;
    if (setup.resource_plan.requests.empty() && setup.renderer) {
        if (const auto* graph = setup.renderer->graph_cache().peek(
                job.metadata.width, job.metadata.height); graph != nullptr) {
            runtime::ResourcePlanner planner;
            for (std::size_t id = 0;
                 id < graph->resource_table().resources.size(); ++id) {
                const auto& allocation = graph->resource_table().resources[id];
                if (allocation.producer == graph::k_invalid_node) {
                    continue;
                }
                const bool persistent = allocation.persistent || allocation.async_use;
                runtime::ResourceRequest request;
                request.id = "GraphNode[" + std::to_string(id) + "]";
                request.desc = allocation.desc;
                request.first = persistent ? 0 : allocation.first_level;
                request.last = persistent ? 0 : allocation.last_level;
                planner.add(std::move(request));
            }
            setup.resource_plan = planner.build();
        }
    }
    const auto& plan_telemetry = setup.resource_plan.telemetry;
    run.logical_resource_count = plan_telemetry.logical_count;
    run.physical_resource_slot_count = plan_telemetry.physical_count;
    run.logical_resource_bytes = plan_telemetry.logical_bytes;
    run.physical_resource_bytes = plan_telemetry.physical_bytes;
    run.alias_saved_bytes = plan_telemetry.alias_saved_bytes;
    run.alias_reuse_count = plan_telemetry.buffer_reuse_count;
    run.new_resource_slot_count = plan_telemetry.buffer_new_allocations;
    run.arena_peak_bytes = plan_telemetry.arena_peak_bytes;
    run.started_at_iso = setup.job_started_iso;
    run.finished_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();

    if (counters) {
        cli::telemetry::populate_run_metrics(run, *counters);
    }

    // Measured startup + per-frame allocation rate (never estimated):
    // process_startup_ms spans process launch → job start; the allocation
    // rate is the framebuffer allocation event count over rendered frames.
    run.process_startup_ms = profiling::duration_ms(process_start_time(), setup.wall_t0);
    run.framebuffer_allocations_per_frame = run.frames_total > 0
        ? static_cast<double>(run.framebuffer_allocations) / static_cast<double>(run.frames_total)
        : 0.0;

    const auto pool_current_bytes =
        setup.renderer->software_framebuffer_pool().current_bytes();
    const auto pool_available_count =
        setup.renderer->software_framebuffer_pool().available_count();
    const auto pool_stats =
        setup.renderer->software_framebuffer_pool().stats();

    std::vector<chronon3d::telemetry::CounterTelemetryRecord> counters_list;
    if (counters) {
        counters_list = cli::telemetry::capture_counters(*counters);
    }
    counters_list.push_back({"pool_current_bytes", pool_current_bytes});
    counters_list.push_back({"pool_available_count", pool_available_count});
    // Export the stats from the renderer-owned pool.  The generic profiling
    // counter can refer to a transient/global pool during video export and
    // must not override the pool that actually served this render.
    const auto configured_pool_budget =
        setup.renderer->runtime().config().cache().fb_pool_budget_bytes();
    counters_list.push_back({
        "framebuffer_pool_budget_bytes",
        configured_pool_budget > 0 ? configured_pool_budget : pool_stats.budget_bytes});
    counters_list.push_back({"framebuffer_pool_retained_bytes", pool_stats.retained_bytes});
    counters_list.push_back({"framebuffer_pool_evicted_count", pool_stats.evicted_count});
    counters_list.push_back({"framebuffer_pool_evicted_bytes", pool_stats.evicted_bytes});
    counters_list.push_back({"framebuffer_pool_pressure_count", pool_stats.pressure_count});
    counters_list.push_back({"framebuffer_pool_size_class_count", pool_stats.size_class_count});

    // GPU backend counters (vkQueueSubmit count + executed command-plan
    // passes).  Software backends contribute nothing; a GPU backend feeds
    // gpu_submissions / passes_executed into the render_counters table.
    if (setup.renderer->runtime().backend_attached()) {
        cli::telemetry::capture_backend_gpu_counters(
            setup.renderer->runtime().backend(), counters_list, run);
    }

    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases = {
        {"setup_renderer", profiling::duration_ms(setup.setup_t0, setup.setup_t1)}
    };
    if (setup.renderer->counters()) {
        auto graph_phases =
            cli::telemetry::capture_graph_phase_records(*setup.renderer->counters());
        phases.insert(phases.end(), graph_phases.begin(), graph_phases.end());
    }
    phases.push_back({"rendering_loop", profiling::duration_ms(loop_t0, loop_t1)});

    // Per-event telemetry stores exist ONLY for the SQLite consumer
    // (TICKET-TELEMETRY-STORE-CONSUMER-AUDIT); this path discards the bundle,
    // so the drain (16 mutex × 7 stores) is gated on the real consumer too.
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    auto telemetry = chronon3d::telemetry::collect_all_telemetry();
    (void)telemetry;
#endif

    if (job.execution.report) {
        cli::telemetry::populate_run_host_attribs(run);
        write_run_to_jsonl(run);

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        auto& tm = chronon3d::telemetry::TelemetryManager::instance();
        tm.initialize_default_stores();
        if (!tm.record_run(run, telemetry_frames, phases, counters_list)) {
            spdlog::warn(
                "[report] TelemetryManager::record_run reported failure for run {}",
                run.run_id);
        }
#endif

        RenderReportContext ctx;
        ctx.run = run;
        ctx.counters = counters_list;
        ctx.phases = phases;
        ctx.frames = telemetry_frames;
        ctx.pool_current_bytes = pool_current_bytes;
        ctx.pool_available_count = pool_available_count;
        ctx.node_cache_top_entries = setup.renderer->node_cache().top_entries_by_weight(10);
        ctx.command_line = job.execution.command_line;
        generate_execution_report(ctx);
    }

    auto& rt = setup.renderer->runtime();
    rt.backend().release_frame_transient_surfaces();
    for (const auto handle : rt.surface_registry().handles_with_lifetime(
             runtime::LifetimeClass::FrameTransient)) {
        (void)rt.surface_registry().release(handle);
    }
    return ok;
}

} // namespace chronon3d::cli
