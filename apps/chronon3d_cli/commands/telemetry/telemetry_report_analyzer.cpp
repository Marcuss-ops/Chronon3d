#include "telemetry_report_analyzer.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli {

AnalysisResult analyze_report_model(const ReportModel& model) {
    AnalysisResult res;

    // Rates
    res.cache_hit_rate = (model.run.cache_hits + model.run.cache_misses) > 0
        ? static_cast<double>(model.run.cache_hits) / static_cast<double>(model.run.cache_hits + model.run.cache_misses)
        : 0.0;
    res.framebuffer_reuse_rate = (model.run.framebuffer_allocations + model.run.framebuffer_reuses) > 0
        ? static_cast<double>(model.run.framebuffer_reuses) / static_cast<double>(model.run.framebuffer_allocations + model.run.framebuffer_reuses)
        : 0.0;
    res.dirty_pixels_share = model.run.pixels_touched > 0
        ? static_cast<double>(model.run.dirty_pixels) / static_cast<double>(model.run.pixels_touched)
        : 0.0;

    // Parse Phase CPU Efficiency counters
    uint64_t tbb_sum = 0, tbb_cnt = 0;
    uint64_t used_par_clear = 0, used_par_transform = 0, used_par_composite = 0;
    uint64_t fb_copy_ms = 0, compositenode_blend_ms = 0, node_exec_ms = 0;

    for (const auto& counter : model.phase_cpu_efficiency_counters) {
        if (counter.name == "tbb_active_workers_avg_sum") tbb_sum = counter.value;
        else if (counter.name == "tbb_active_workers_avg_count") tbb_cnt = counter.value;
        else if (counter.name == "used_parallel_clear") used_par_clear = counter.value;
        else if (counter.name == "used_parallel_transform") used_par_transform = counter.value;
        else if (counter.name == "used_parallel_composite") used_par_composite = counter.value;
        else if (counter.name == "framebuffer_copy_ms") fb_copy_ms = counter.value;
        else if (counter.name == "compositenode_blend_ms") compositenode_blend_ms = counter.value;
        else if (counter.name == "node_execute_actual_ms") node_exec_ms = counter.value;
        else if (counter.name == "level_parallel_count") res.lev_par = counter.value;
        else if (counter.name == "level_sequential_count") res.lev_seq = counter.value;
        else if (counter.name == "parallel_regions_skipped_small_level") res.lev_skip = counter.value;
    }

    if (tbb_cnt > 0) {
        res.tbb_avg_workers = static_cast<double>(tbb_sum) / static_cast<double>(tbb_cnt);
    }

    // Helper for phase efficiency line
    const uint64_t conv_pixels = (model.run.frame_conversion_copy_ms > 0) ? model.run.pixels_touched : 0;
    const auto make_phase_efficiency = [&](const std::string& name, double wall_ms, uint64_t pixels, double est_cores) {
        PhaseEfficiencyResult pe;
        pe.phase_name = name;
        pe.wall_ms = wall_ms;
        pe.pixels = pixels;
        pe.est_cores = est_cores;
        pe.cpu_ms = wall_ms * est_cores;
        const double bytes = static_cast<double>(pixels) * 16.0;
        const double wall_s = wall_ms / 1000.0;
        pe.gb_s = wall_s > 0.0 ? (bytes / wall_s) / 1.0e9 : 0.0;
        return pe;
    };

    if (model.run.clearnode_ms > 0 || model.run.clear_pixels > 0) {
        res.phase_efficiencies.push_back(make_phase_efficiency(
            "clear_node", static_cast<double>(model.run.clearnode_ms),
            model.run.clear_pixels, used_par_clear > 0 ? res.tbb_avg_workers : 1.0
        ));
    }

    const double transform_share = (model.run.pixels_touched > 0 && model.run.transform_pixels > 0)
        ? std::min(1.0, static_cast<double>(model.run.transform_pixels) / static_cast<double>(model.run.pixels_touched))
        : 0.0;
    const double transform_wall = static_cast<double>(node_exec_ms) * transform_share;
    if (transform_wall > 0.0 || model.run.transform_pixels > 0) {
        res.phase_efficiencies.push_back(make_phase_efficiency(
            "transform_node", transform_wall,
            model.run.transform_pixels, used_par_transform > 0 ? res.tbb_avg_workers : 1.0
        ));
    }

    if (compositenode_blend_ms > 0 || model.run.composite_pixels > 0) {
        res.phase_efficiencies.push_back(make_phase_efficiency(
            "composite_node", static_cast<double>(compositenode_blend_ms),
            model.run.composite_pixels, used_par_composite > 0 ? res.tbb_avg_workers : 1.0
        ));
    }

    if (model.run.frame_conversion_copy_ms > 0 || conv_pixels > 0) {
        res.phase_efficiencies.push_back(make_phase_efficiency(
            "frame_conversion", static_cast<double>(model.run.frame_conversion_copy_ms),
            conv_pixels, res.tbb_avg_workers
        ));
    }

    if (fb_copy_ms > 0) {
        res.phase_efficiencies.push_back(make_phase_efficiency(
            "framebuffer_copy", static_cast<double>(fb_copy_ms),
            model.run.pixels_touched, res.tbb_avg_workers
        ));
    }

    // Hot Nodes coverage
    res.hot_nodes_coverage = node_exec_ms > 0
        ? (static_cast<double>(model.hot_nodes_total_ms) / static_cast<double>(node_exec_ms)) * 100.0
        : 0.0;
    res.hot_nodes_low_coverage = (res.hot_nodes_coverage < 80.0);

    // Hot Work Attribution
    uint64_t clr_memcpy_ms = 0, clr_clear_ms = 0, clr_restore_ms = 0;
    uint64_t blend_ms = compositenode_blend_ms;
    uint64_t conv_ms = model.run.frame_conversion_copy_ms;
    uint64_t pipe_ms = model.run.video_pipe_write_ms;

    for (const auto& counter : model.hot_work_attribution_counters) {
        if (counter.name == "clearnode_memcpy_ms") clr_memcpy_ms = counter.value;
        else if (counter.name == "clearnode_clear_ms") clr_clear_ms = counter.value;
        else if (counter.name == "clearnode_restore_ms") clr_restore_ms = counter.value;
        else if (counter.name == "compositenode_blend_ms") blend_ms = counter.value;
        else if (counter.name == "frame_conversion_copy_ms") conv_ms = counter.value;
        else if (counter.name == "video_pipe_write_ms") pipe_ms = counter.value;
    }

    const auto make_work_attribution = [&](const std::string& name, uint64_t wall_ms,
                                            uint64_t pixels, double est_cores,
                                            bool blocked_io = false) {
        WorkAttributionResult wa;
        wa.work_name = name;
        wa.wall_ms = static_cast<double>(wall_ms);
        wa.pixels = pixels;
        wa.est_cores = est_cores;
        wa.cpu_ms = wa.wall_ms * est_cores;
        wa.blocked_io = blocked_io;
        const double bytes = static_cast<double>(pixels) * 16.0;
        const double wall_s = wa.wall_ms / 1000.0;
        wa.gb_s = wall_s > 0.0 ? (bytes / wall_s) / 1.0e9 : 0.0;
        return wa;
    };

    if (clr_memcpy_ms > 0 || model.run.clearnode_copy_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "ClearNode memcpy", clr_memcpy_ms, model.run.clearnode_copy_pixels,
            used_par_clear > 0 ? res.tbb_avg_workers : 1.0
        ));
    }
    if (clr_clear_ms > 0 || model.run.clear_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "ClearNode clear", clr_clear_ms, model.run.clear_pixels,
            used_par_clear > 0 ? res.tbb_avg_workers : 1.0
        ));
    }
    if (clr_restore_ms > 0 || model.run.clearnode_copy_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "ClearNode restore", clr_restore_ms, model.run.clearnode_copy_pixels,
            used_par_clear > 0 ? res.tbb_avg_workers : 1.0
        ));
    }
    if (blend_ms > 0 || model.run.composite_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "Composite blend", blend_ms, model.run.composite_pixels,
            used_par_composite > 0 ? res.tbb_avg_workers : 1.0
        ));
    }

    const uint64_t transform_wall_u64 = static_cast<uint64_t>(
        static_cast<double>(node_exec_ms) * transform_share);
    if (transform_wall_u64 > 0 || model.run.transform_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "Transform rows", transform_wall_u64, model.run.transform_pixels,
            used_par_transform > 0 ? res.tbb_avg_workers : 1.0
        ));
    }
    if (conv_ms > 0 || conv_pixels > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "Frame conversion", conv_ms, conv_pixels, res.tbb_avg_workers
        ));
    }
    if (pipe_ms > 0 || model.run.pixels_touched > 0) {
        res.work_attributions.push_back(make_work_attribution(
            "Pipe write", pipe_ms, model.run.pixels_touched, 1.0, true
        ));
    }

    const uint64_t total_attributed =
        clr_memcpy_ms + clr_clear_ms + clr_restore_ms + blend_ms +
        transform_wall_u64 + conv_ms + pipe_ms;
    res.attribution_coverage = node_exec_ms > 0
        ? (static_cast<double>(total_attributed) / static_cast<double>(node_exec_ms)) * 100.0
        : 0.0;
    res.attribution_low_coverage = (res.attribution_coverage < 80.0);

    // System Resources
    for (const auto& counter : model.system_resources) {
        if (counter.name == "system_logical_cores") res.logical_cores = counter.value;
        else if (counter.name == "system_ram_total_mb") res.ram_total_mb = counter.value;
        else if (counter.name == "system_ram_available_min_mb") res.ram_avail_min_mb = counter.value;
        else if (counter.name == "process_cpu_user_ms") res.cpu_user_ms = counter.value;
        else if (counter.name == "process_cpu_sys_ms") res.cpu_sys_ms = counter.value;
        else if (counter.name == "process_rss_peak_mb") res.rss_peak_mb = counter.value;
        else if (counter.name == "tbb_arena_max_concurrency") res.tbb_arena = counter.value;
        else if (counter.name == "tbb_active_workers_peak") res.tbb_peak = counter.value;
        else if (counter.name == "tbb_active_workers_avg_sum") res.tbb_avg_sum = counter.value;
        else if (counter.name == "tbb_active_workers_avg_count") res.tbb_avg_count = counter.value;
        else if (counter.name == "parallel_regions_count") res.parallel_count = counter.value;
        else if (counter.name == "parallel_regions_skipped_small_level") res.parallel_skipped = counter.value;
    }

    res.cpu_total_ms = res.cpu_user_ms + res.cpu_sys_ms;
    const double wall_ms = model.run.wall_time_ms;
    if (res.cpu_total_ms > 0 && wall_ms > 0 && res.logical_cores > 0) {
        res.effective_cores = static_cast<double>(res.cpu_total_ms) / wall_ms;
        res.cpu_util_pct = res.effective_cores / static_cast<double>(res.logical_cores) * 100.0;
    }

    // Diagnostics statuses
    if (res.cpu_total_ms > 0 && wall_ms > 0 && res.logical_cores > 0) {
        res.cpu_saturation_status = (res.cpu_util_pct >= 65.0) ? "GOOD"
                                  : (res.cpu_util_pct >= 40.0) ? "MODERATE"
                                  : "LIMITED";
    } else {
        res.cpu_saturation_status = "UNKNOWN";
    }

    if (res.rss_peak_mb > 0 && res.ram_avail_min_mb > 0) {
        const double ratio = static_cast<double>(res.rss_peak_mb) / static_cast<double>(res.ram_avail_min_mb);
        res.memory_pressure_status = (ratio < 0.25) ? "SAFE"
                                   : (ratio < 0.50) ? "MODERATE"
                                   : "WARNING";
    } else {
        res.memory_pressure_status = "UNKNOWN";
    }

    res.encoder_blocked_pct = model.run.render_ms > 0
        ? model.run.ffmpeg_pipe_write_blocked_ms / model.run.render_ms * 100.0 : 0.0;
    res.encoder_pipe_status = (res.encoder_blocked_pct < 10.0) ? "GOOD"
                            : (res.encoder_blocked_pct < 25.0) ? "MODERATE BOTTLENECK"
                            : "MAJOR BOTTLENECK";

    res.graph_parallelism_status = (res.lev_par > res.lev_seq) ? "STRONG"
                                 : (res.lev_par > 0 && res.lev_par == res.lev_seq) ? "MODERATE"
                                 : (res.lev_seq > res.lev_par) ? "LIMITED BY DAG"
                                 : "UNKNOWN";

    const bool any_par = (used_par_clear > 0 || used_par_transform > 0 || used_par_composite > 0);
    res.node_parallelism_status = (any_par && res.tbb_peak >= 4) ? "STRONG"
                                : (any_par) ? "MODERATE"
                                : "WEAK";

    res.conversion_share_pct = model.run.render_ms > 0
        ? model.run.frame_conversion_copy_ms / model.run.render_ms * 100.0 : 0.0;
    res.frame_conversion_status = (res.conversion_share_pct < 10.0) ? "GOOD"
                                : (res.conversion_share_pct < 20.0) ? "MODERATE BOTTLENECK"
                                : "MAJOR BOTTLENECK";

    res.cache_efficiency_status = (res.cache_hit_rate >= 0.80) ? "EXCELLENT"
                                : (res.cache_hit_rate >= 0.50) ? "GOOD"
                                : "MODERATE";

    const double dr = (model.run.pixels_touched > 0)
        ? static_cast<double>(model.run.dirty_pixels) / static_cast<double>(model.run.pixels_touched) : 1.0;
    res.dirty_rect_efficiency_status = (dr <= 0.20) ? "EXCELLENT"
                                     : (dr <= 0.50) ? "GOOD"
                                     : "MODERATE";

    // Explained coverage for attribution
    double attribution_cov = node_exec_ms > 0
        ? static_cast<double>(total_attributed) / static_cast<double>(node_exec_ms) * 100.0 : 0.0;
    res.hot_attribution_status = (attribution_cov >= 90.0) ? "EXCELLENT"
                               : (attribution_cov >= 80.0) ? "GOOD"
                               : "NEEDS WORK";

    // Primary bottleneck
    if (res.encoder_blocked_pct >= 25.0) {
        res.primary_bottleneck = "Encoder pipe (FFmpeg subprocess).";
        res.recommendation = "Increase encoder queue depth or use native encoder path.";
    } else if (res.conversion_share_pct >= 20.0) {
        res.primary_bottleneck = "Frame conversion (RGBA->YUV).";
        res.recommendation = "Benchmark Highway SIMD vs libyuv; consider direct YUV path.";
    } else if (res.cpu_util_pct < 40.0 && res.logical_cores > 0) {
        res.primary_bottleneck = "CPU underutilization.";
        res.recommendation = fmt::format(
            "Increase parallelism thresholds or enable frame-level parallelism. Do not increase render workers beyond {} to leave margin for OS + encoder.",
            res.logical_cores > 2 ? res.logical_cores - 2 : 1
        );
    } else {
        res.primary_bottleneck = "No single dominant bottleneck — system is well-balanced for this workload.";
        res.recommendation = "";
    }

    // Warnings
    if (model.run.os_page_faults_major > 0) {
        res.warnings.push_back(fmt::format("Major page faults detected ({}). Setup is hitting disk I/O.", model.run.os_page_faults_major));
    }
    if (model.run.process_context_switches_involuntary > model.run.process_context_switches_voluntary * 2) {
        res.warnings.push_back("High involuntary context switches. CPU contention detected.");
    }

    return res;
}

} // namespace chronon3d::cli
#endif
