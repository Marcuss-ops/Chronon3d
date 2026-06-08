#include "render_job_detail.hpp"
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

            // ── Hot Work Attribution ────────────────────────────────────
            // Extract phase-cost and decision counters from counters_list.
            auto get_counter = [&](const char* name) -> uint64_t {
                for (const auto& c : counters_list) {
                    if (c.counter_name == name) return c.counter_value;
                }
                return 0;
            };
            const uint64_t clr_memcpy = get_counter("clearnode_memcpy_ms");
            const uint64_t clr_clear  = get_counter("clearnode_clear_ms");
            const uint64_t c_blend    = get_counter("compositenode_blend_ms");
            const uint64_t conv_ms    = get_counter("frame_conversion_copy_ms");
            const uint64_t pipe_ms    = get_counter("video_pipe_write_ms");
            const uint64_t n_exec     = get_counter("node_execute_actual_ms");
            const uint64_t clear_px   = get_counter("clear_pixels");
            const uint64_t copy_px    = get_counter("clearnode_copy_pixels");
            const uint64_t comp_px    = get_counter("composite_pixels");
            const uint64_t xform_px   = get_counter("transform_pixels");
            const uint64_t touched_px = get_counter("pixels_touched");
            const uint64_t used_par_c = get_counter("used_parallel_clear");
            const uint64_t used_par_t = get_counter("used_parallel_transform");
            const uint64_t used_par_p = get_counter("used_parallel_composite");
            const uint64_t tbb_sum    = get_counter("tbb_active_workers_avg_sum");
            const uint64_t tbb_cnt    = get_counter("tbb_active_workers_avg_count");
            const double tbb_avg = tbb_cnt > 0
                ? static_cast<double>(tbb_sum) / static_cast<double>(tbb_cnt) : 1.0;
            const double xform_share = (touched_px > 0 && xform_px > 0)
                ? std::min(1.0, static_cast<double>(xform_px) / static_cast<double>(touched_px)) : 0.0;
            const uint64_t xform_wall = static_cast<uint64_t>(
                static_cast<double>(n_exec) * xform_share);
            const uint64_t conv_pixels = (conv_ms > 0) ? touched_px : 0;

            report_out << "\n--- HOT WORK ATTRIBUTION ---\n";
            report_out << std::left << std::setw(22) << "Work"
                       << std::right << std::setw(10) << "Wall"
                       << std::setw(12) << "Pixels"
                       << std::setw(12) << "Est.Cores"
                       << std::setw(12) << "CPU ms"
                       << std::setw(10) << "GB/s" << "\n";
            report_out << std::string(78, '-') << "\n";

            auto attr_line = [&](const char* name, uint64_t w_ms, uint64_t px,
                                  double cores, const char* gbs = nullptr) {
                if (w_ms == 0 && px == 0) return;
                const double w  = static_cast<double>(w_ms);
                const double cpu = w * cores;
                const double by  = static_cast<double>(px) * 16.0;
                const double ws  = w / 1000.0;
                const double gb  = ws > 0.0 ? (by / ws) / 1.0e9 : 0.0;
                report_out << std::left << std::setw(22) << name
                           << std::right << std::setw(10) << fmt::format("{:.0f}", w)
                           << std::setw(12) << px
                           << std::setw(12) << fmt::format("{:.2f}", cores)
                           << std::setw(12) << fmt::format("{:.0f}", cpu);
                if (gbs) {
                    report_out << std::setw(10) << gbs;
                } else {
                    report_out << std::setw(10) << fmt::format("{:.2f}", gb);
                }
                report_out << "\n";
            };

            attr_line("ClearNode memcpy",  clr_memcpy, copy_px,   used_par_c ? tbb_avg : 1.0);
            attr_line("ClearNode clear",   clr_clear,  clear_px,  used_par_c ? tbb_avg : 1.0);
            attr_line("Composite blend",   c_blend,    comp_px,   used_par_p ? tbb_avg : 1.0);
            attr_line("Transform rows",    xform_wall, xform_px,  used_par_t ? tbb_avg : 1.0);
            attr_line("Frame conversion",  conv_ms,    conv_pixels, tbb_avg);
            attr_line("Pipe write",        pipe_ms,    touched_px, 1.0, "blocked");

            const uint64_t avoided = get_counter("clearnode_bytes_avoided");
            if (avoided > 0) {
                report_out << std::left << std::setw(22) << "Bytes avoided (COW)"
                           << std::right << std::setw(10) << "—"
                           << std::setw(12) << "—"
                           << std::setw(12) << "—"
                           << std::setw(12) << "—"
                           << std::setw(10)
                           << fmt::format("{:.2f} GB", static_cast<double>(avoided) / 1e9)
                           << "\n";
            }

            // ── ClearNode Copy Diagnostics ─────────────────────────────
            const uint64_t memcpy_bytes   = get_counter("clearnode_memcpy_bytes");
            const uint64_t memcpy_calls   = get_counter("clearnode_memcpy_calls");
            const uint64_t detach_count   = get_counter("clearnode_detach_shared_count");
            const uint64_t partial_count  = get_counter("clearnode_partial_clip_copy_count");
            const uint64_t skip_count     = get_counter("clearnode_full_clip_skip_count");
            const uint64_t use_sum        = get_counter("prev_fb_use_count_sum");
            const uint64_t use_samples    = get_counter("prev_fb_use_count_samples");
            const uint64_t use_peak       = get_counter("prev_fb_use_count_peak");
            const uint64_t comp_copy_px   = get_counter("composite_copy_pixels");
            if (memcpy_calls > 0 || detach_count > 0 || skip_count > 0 || comp_copy_px > 0) {
                report_out << "\n--- CLEARNODE COPY DIAGNOSTICS ---\n";
                report_out << "clearnode_memcpy_calls           : " << memcpy_calls << "\n";
                report_out << "clearnode_memcpy_bytes           : " << fmt::format("{:.2f} GB", static_cast<double>(memcpy_bytes) / 1e9) << "\n";
                report_out << "clearnode_detach_shared_count    : " << detach_count << "\n";
                report_out << "clearnode_partial_clip_copy_count: " << partial_count << "\n";
                report_out << "clearnode_full_clip_skip_count   : " << skip_count << "\n";
                report_out << "prev_fb_use_count_peak           : " << use_peak << "\n";
                if (use_samples > 0) {
                    report_out << "prev_fb_use_count_avg            : "
                               << fmt::format("{:.2f}", static_cast<double>(use_sum) / static_cast<double>(use_samples)) << "\n";
                }
                report_out << "composite_copy_pixels            : " << comp_copy_px << "\n";
                report_out << "clearnode_copy_pixels            : " << copy_px << "\n";
            }

            const uint64_t total_attr = clr_memcpy + clr_clear + c_blend + xform_wall + conv_ms + pipe_ms;
            if (n_exec > 0) {
                const double cov = static_cast<double>(total_attr) / static_cast<double>(n_exec) * 100.0;
                report_out << "\nAttribution coverage: " << total_attr << " / " << n_exec
                           << " (" << fmt::format("{:.1f}%", cov) << ")\n";
            }

            // ── Bottleneck Diagnosis ──────────────────────────────────
            // Classify each subsystem based on counter thresholds.
            const uint64_t sys_cores  = get_counter("system_logical_cores");
            const uint64_t cpu_user   = get_counter("process_cpu_user_ms");
            const uint64_t cpu_sys    = get_counter("process_cpu_sys_ms");
            const uint64_t rss_peak   = get_counter("process_rss_peak_mb");
            const uint64_t ram_avail  = get_counter("system_ram_available_min_mb");
            const uint64_t ffmpeg_blk = get_counter("ffmpeg_pipe_write_blocked_ms");
            const uint64_t lev_par_c  = get_counter("level_parallel_count");
            const uint64_t lev_seq_c  = get_counter("level_sequential_count");
            const uint64_t cache_h    = get_counter("cache_hits");
            const uint64_t cache_m    = get_counter("cache_misses");
            const uint64_t dirty_px   = get_counter("dirty_pixels");
            const uint64_t tbb_peak_c = get_counter("tbb_active_workers_peak");

            const uint64_t cpu_total  = cpu_user + cpu_sys;
            const double eff_cores = (wall_time_ms > 0 && cpu_total > 0)
                ? static_cast<double>(cpu_total) / wall_time_ms : 0.0;
            const double cpu_util = (sys_cores > 0)
                ? eff_cores / static_cast<double>(sys_cores) * 100.0 : 0.0;
            const double cache_rate = (cache_h + cache_m) > 0
                ? static_cast<double>(cache_h) / static_cast<double>(cache_h + cache_m) : 0.0;
            const double dirty_ratio = (touched_px > 0)
                ? static_cast<double>(dirty_px) / static_cast<double>(touched_px) : 1.0;
            const double render_ms_d = total_render_ms > 0 ? total_render_ms : run.render_ms;
            const double ffmpeg_share = render_ms_d > 0
                ? static_cast<double>(ffmpeg_blk) / render_ms_d * 100.0 : 0.0;
            const double attr_cov = n_exec > 0
                ? static_cast<double>(total_attr) / static_cast<double>(n_exec) * 100.0 : 0.0;

            auto status = [](const char* label, const char* st) {
                return fmt::format("{:<26} {}", label, st);
            };

            report_out << "\n--- BOTTLENECK DIAGNOSIS ---\n";

            // CPU saturation
            const char* cpu_st = (cpu_util >= 65.0) ? "GOOD"
                               : (cpu_util >= 40.0) ? "MODERATE"
                               : "LIMITED";
            report_out << status("CPU saturation", cpu_st)
                       << fmt::format("  ({:.1f}% of {} cores)", cpu_util, sys_cores) << "\n";

            // Memory pressure
            const char* mem_st = (ram_avail > 0 && rss_peak > 0)
                ? ((static_cast<double>(rss_peak) / static_cast<double>(ram_avail) < 0.25) ? "SAFE"
                 : (static_cast<double>(rss_peak) / static_cast<double>(ram_avail) < 0.50) ? "MODERATE"
                 : "WARNING")
                : "UNKNOWN";
            report_out << status("Memory pressure", mem_st);
            if (rss_peak > 0 && ram_avail > 0)
                report_out << fmt::format("  ({} MB RSS / {} MB avail)", rss_peak, ram_avail);
            report_out << "\n";

            // Encoder pipe
            const char* enc_st = (ffmpeg_share < 10.0) ? "GOOD"
                               : (ffmpeg_share < 25.0) ? "MODERATE BOTTLENECK"
                               : "MAJOR BOTTLENECK";
            report_out << status("Encoder pipe", enc_st)
                       << fmt::format("  ({:.0f}% of render time blocked)", ffmpeg_share) << "\n";

            // Graph parallelism
            const char* gph_st = (lev_par_c > lev_seq_c) ? "STRONG"
                               : (lev_par_c > 0 && lev_par_c == lev_seq_c) ? "MODERATE"
                               : (lev_seq_c > lev_par_c) ? "LIMITED BY DAG"
                               : "UNKNOWN";
            report_out << status("Graph parallelism", gph_st)
                       << fmt::format("  ({} parallel / {} sequential levels)", lev_par_c, lev_seq_c) << "\n";

            // Node parallelism
            const bool any_par = (used_par_c > 0 || used_par_t > 0 || used_par_p > 0);
            const char* nod_st = (any_par && tbb_peak_c >= 4) ? "STRONG"
                               : (any_par) ? "MODERATE"
                               : "WEAK";
            report_out << status("Node parallelism", nod_st)
                       << fmt::format("  (TBB peak: {} workers)", tbb_peak_c) << "\n";

            // Frame conversion (separate from encoder — this is the YUV conversion cost)
            const double conv_share = render_ms_d > 0
                ? static_cast<double>(conv_ms) / render_ms_d * 100.0 : 0.0;
            const char* cnv_st = (conv_share < 10.0) ? "GOOD"
                               : (conv_share < 20.0) ? "MODERATE BOTTLENECK"
                               : "MAJOR BOTTLENECK";
            report_out << status("Frame conversion", cnv_st)
                       << fmt::format("  ({:.0f}% of render time)", conv_share) << "\n";

            // Cache efficiency
            const char* ch_st = (cache_rate >= 0.80) ? "EXCELLENT"
                              : (cache_rate >= 0.50) ? "GOOD"
                              : "MODERATE";
            report_out << status("Cache efficiency", ch_st)
                       << fmt::format("  ({:.0f}% hit rate)", cache_rate * 100.0) << "\n";

            // Dirty rect efficiency
            const char* dr_st = (dirty_ratio <= 0.20) ? "EXCELLENT"
                              : (dirty_ratio <= 0.50) ? "GOOD"
                              : "MODERATE";
            report_out << status("Dirty rect efficiency", dr_st)
                       << fmt::format("  ({:.0f}% dirty)", dirty_ratio * 100.0) << "\n";

            // Hot attribution
            const char* ha_st = (attr_cov >= 90.0) ? "EXCELLENT"
                              : (attr_cov >= 80.0) ? "GOOD"
                              : "NEEDS WORK";
            report_out << status("Hot attribution", ha_st)
                       << fmt::format("  ({:.0f}% explained)", attr_cov) << "\n";

            // Primary bottleneck summary
            report_out << "\n";
            if (ffmpeg_share >= 25.0) {
                report_out << "Primary bottleneck: Encoder pipe (FFmpeg subprocess).\n";
                report_out << "Recommendation: Increase encoder queue depth or use native encoder path.\n";
            } else if (conv_share >= 20.0) {
                report_out << "Primary bottleneck: Frame conversion (RGBA→YUV).\n";
                report_out << "Recommendation: Benchmark Highway SIMD vs libyuv; consider direct YUV path.\n";
            } else if (cpu_util < 40.0) {
                report_out << "Primary bottleneck: CPU underutilization.\n";
                report_out << "Recommendation: Increase parallelism thresholds or enable frame-level parallelism.\n";
                report_out << "Do not increase render workers beyond " << (sys_cores > 0 ? sys_cores - 2 : 6)
                           << " to leave margin for OS + encoder.\n";
            } else {
                report_out << "No single dominant bottleneck — system is well-balanced for this workload.\n";
            }

            spdlog::info("Execution report saved to {}", file_name.str());
        }
    }

    return ok;
}

} // namespace chronon3d::cli
