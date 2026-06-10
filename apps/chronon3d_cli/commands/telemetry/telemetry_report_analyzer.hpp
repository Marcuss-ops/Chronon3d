#pragma once

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include "telemetry_report_model.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace chronon3d::cli {

struct PhaseEfficiencyResult {
    std::string phase_name;
    double wall_ms = 0.0;
    uint64_t pixels = 0;
    double est_cores = 0.0;
    double cpu_ms = 0.0;
    double gb_s = 0.0;
};

struct WorkAttributionResult {
    std::string work_name;
    double wall_ms = 0.0;
    uint64_t pixels = 0;
    double est_cores = 0.0;
    double cpu_ms = 0.0;
    double gb_s = 0.0;
    bool blocked_io = false;
};

struct AnalysisResult {
    double cache_hit_rate = 0.0;
    double framebuffer_reuse_rate = 0.0;
    double dirty_pixels_share = 0.0;
    double tbb_avg_workers = 1.0;

    std::vector<PhaseEfficiencyResult> phase_efficiencies;

    double hot_nodes_coverage = 0.0;
    bool hot_nodes_low_coverage = false;

    std::vector<WorkAttributionResult> work_attributions;
    double attribution_coverage = 0.0;
    bool attribution_low_coverage = false;

    // Parallelism levels
    uint64_t lev_par = 0;
    uint64_t lev_seq = 0;
    uint64_t lev_skip = 0;

    // Resource diagnostics
    uint64_t logical_cores = 0;
    uint64_t ram_total_mb = 0;
    uint64_t ram_avail_min_mb = 0;
    uint64_t rss_peak_mb = 0;
    uint64_t cpu_user_ms = 0;
    uint64_t cpu_sys_ms = 0;
    uint64_t cpu_total_ms = 0;
    double effective_cores = 0.0;
    double cpu_util_pct = 0.0;
    uint64_t tbb_arena = 0;
    uint64_t tbb_peak = 0;
    uint64_t tbb_avg_sum = 0;
    uint64_t tbb_avg_count = 0;
    uint64_t parallel_count = 0;
    uint64_t parallel_skipped = 0;

    // Bottleneck diagnosis statuses
    std::string cpu_saturation_status;
    std::string memory_pressure_status;
    std::string encoder_pipe_status;
    double encoder_blocked_pct = 0.0;
    std::string graph_parallelism_status;
    std::string node_parallelism_status;
    std::string frame_conversion_status;
    double conversion_share_pct = 0.0;
    std::string cache_efficiency_status;
    std::string dirty_rect_efficiency_status;
    std::string hot_attribution_status;

    std::string primary_bottleneck;
    std::string recommendation;

    std::vector<std::string> warnings;
};

AnalysisResult analyze_report_model(const ReportModel& model);

} // namespace chronon3d::cli
#endif
