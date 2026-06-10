#pragma once

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>

namespace chronon3d::cli {

using RunSummary = chronon3d::telemetry::RenderTelemetryRecord;

struct FrameSummaryData {
    int64_t count = 0;
    double avg_duration_ms = 0.0;
    double min_duration_ms = 0.0;
    double max_duration_ms = 0.0;
    double avg_cache_hit = 0.0;
    double avg_dirty_area_ratio = 0.0;
};

struct ActiveCachedBreakdownData {
    double avg_active_ms = 0.0;
    int64_t active_count = 0;
    double avg_cached_ms = 0.0;
    int64_t cached_count = 0;
};

struct PhaseDuration {
    std::string phase_name;
    double duration_ms = 0.0;
};

struct HotNodeData {
    std::string node_name;
    std::string node_type;
    int64_t calls = 0;
    double duration_ms = 0.0;
    double avg_duration_ms = 0.0;
    int64_t cache_hits = 0;
    int64_t cache_ops = 0;
    int64_t pixels_touched = 0;
};

struct LayerCostData {
    std::string layer_name;
    std::string layer_type;
    int64_t calls = 0;
    double duration_ms = 0.0;
    int64_t visible_pixels = 0;
    int64_t dirty_pixels = 0;
    int64_t glyphs_rasterized = 0;
    int64_t images_sampled = 0;
};

struct FrameSampleData {
    int64_t frame_number = 0;
    double duration_ms = 0.0;
    bool cache_hit = false;
    double dirty_area_ratio = 0.0;
    double graph_eval_ms = 0.0;
    double queue_wait_ms = 0.0;
    double conversion_copy_ms = 0.0;
    double encoder_ms = 0.0;
    double pipe_write_ms = 0.0;
    double native_convert_ms = 0.0;
    double native_send_ms = 0.0;
    double native_receive_ms = 0.0;
    double native_mux_ms = 0.0;
    bool dirty_rect_enabled = false;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    bool tile_execution_used = false;
    bool fast_path_reused = false;
    bool graph_reused = false;
};

struct CacheDiagnosticData {
    std::string cache_status;
    int64_t count = 0;
};

struct NameValueCounter {
    std::string name;
    uint64_t value = 0;
};

struct ReportModel {
    std::string run_id;
    RunSummary run;

    FrameSummaryData frame_summary;
    ActiveCachedBreakdownData active_cached_breakdown;
    std::vector<PhaseDuration> core_render_phases;
    std::vector<NameValueCounter> phase_cpu_efficiency_counters;
    std::vector<HotNodeData> hot_nodes;
    std::vector<NameValueCounter> phase_cost_counters;
    uint64_t hot_nodes_total_ms = 0;
    std::vector<NameValueCounter> hot_work_attribution_counters;
    uint64_t bytes_avoided = 0;
    std::vector<LayerCostData> layer_cost_breakdown;
    std::vector<FrameSampleData> frame_samples;
    std::vector<CacheDiagnosticData> cache_diagnostics;
    std::vector<NameValueCounter> parallelism_decisions;
    std::vector<NameValueCounter> system_resources;
    std::vector<NameValueCounter> framebuffer_pool;
};

ReportModel load_report_model(sqlite3* db, const std::string& run_id, const RunSummary& run);

} // namespace chronon3d::cli
#endif
