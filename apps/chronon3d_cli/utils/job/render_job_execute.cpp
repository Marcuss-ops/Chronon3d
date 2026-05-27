#include "render_job_detail.hpp"
#include "../telemetry/telemetry_run.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

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
    // Renderer warmup (preallocate framebuffers + optional dummy frame)
    if (plan.warmup_renderer) {
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
    }
    renderer->counters()->reset();
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

    // Initialize the default telemetry manager stores
    chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    double total_render_ms = 0.0;
    double total_encode_ms = 0.0;
    int frames_written = 0;

    const int64_t effective_end = (plan.range.start == plan.range.end) ? plan.range.start + 1 : plan.range.end;
    bool ok = true;

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
        {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()},
        {"rendering_loop", std::chrono::duration<double, std::milli>(loop_t1 - loop_t0).count()}
    };

    // Flush per-node telemetry collected during graph execution
    auto node_events = chronon3d::telemetry::collect_node_telemetry();
    auto layer_events = chronon3d::telemetry::collect_layer_telemetry();
    auto cache_events = chronon3d::telemetry::collect_cache_telemetry();
    auto culling_events = chronon3d::telemetry::collect_culling_telemetry();
    auto text_events = chronon3d::telemetry::collect_text_telemetry();
    auto image_events = chronon3d::telemetry::collect_image_telemetry();
    auto tile_events = chronon3d::telemetry::collect_tile_telemetry();

    chronon3d::telemetry::TelemetryManager::instance().record_run(run, telemetry_frames, phases, counters_list,
                                                        node_events, layer_events,
                                                        cache_events, culling_events,
                                                        text_events, image_events,
                                                        tile_events);

    if (plan.report) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &now_time);
#else
        localtime_r(&now_time, &tm_buf);
#endif
        std::ostringstream file_name;
        file_name << "chronon3d-" 
                  << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") 
                  << ".log";

        std::ofstream report_out(file_name.str());
        if (report_out) {
            report_out << "==========================================\n";
            report_out << "       CHRONON3D RUN EXECUTION REPORT     \n";
            report_out << "==========================================\n\n";
            report_out << "Command Line: " << cli::g_command_line << "\n";
            report_out << "Timestamp:    " << run.finished_at_iso << "\n";
            report_out << "Git Commit:   " << run.git_commit_short << "\n";
            report_out << "OS:           " << run.os << "\n";
            report_out << "CPU Model:    " << run.cpu_model << "\n";
            report_out << "Cores:        " << run.cores << "\n";
            report_out << "Compiler:     " << run.compiler_info << "\n";
            report_out << "Build Type:   " << run.build_type << "\n\n";
            report_out << "Run ID:         " << run.run_id << "\n";
            report_out << "Composition ID: " << run.composition_id << "\n";
            report_out << "Output Path:    " << run.output_path << "\n";
            report_out << "Frames Total:   " << run.frames_total << "\n";
            report_out << "Wall Time:      " << run.wall_time_ms << " ms\n";
            report_out << "Render Time:    " << run.render_ms << " ms\n";
            report_out << "Effective FPS:  " << run.effective_fps << "\n\n";
            report_out << "--- PERFORMANCE COUNTERS ---\n";
            for (const auto& c : counters_list) {
                report_out << std::left << std::setw(28) << c.counter_name << ": " << c.counter_value << "\n";
            }
            report_out << "\n--- PHASE DURATIONS ---\n";
            for (const auto& p : phases) {
                report_out << std::left << std::setw(28) << p.phase_name << ": " << p.duration_ms << " ms\n";
            }
            report_out << "\n--- FRAME BREAKDOWN ---\n";
            for (const auto& f : telemetry_frames) {
                report_out << "Frame " << std::setw(4) << f.frame_number 
                           << " | Dur: " << std::setw(8) << f.duration_ms << " ms"
                           << " | Cache Hit: " << (f.cache_hit ? "YES" : "NO ")
                           << " | Dirty Ratio: " << f.dirty_area_ratio << "\n";
            }
            spdlog::info("Execution report saved to {}", file_name.str());
        }
    }

    return ok;
}

} // namespace chronon3d::cli
