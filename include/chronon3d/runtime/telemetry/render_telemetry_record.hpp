#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace chronon3d::telemetry {

struct RenderTelemetryRecord {
    std::string run_id;
    std::string composition_id;
    std::string output_path;

    bool success{false};
    std::string error_code;
    std::string error_message;

    int frames_total{0};
    int frames_written{0};

    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
    double effective_fps{0.0};

    // System-wide performance counters mapped from RenderCounters
    uint64_t pixels_touched{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t nodes_executed{0};
    uint64_t layers_rendered{0};
    uint64_t text_glyphs_rasterized{0};
    uint64_t images_sampled{0};
    uint64_t blur_pixels{0};
    uint64_t simd_lerp_calls{0};
    uint64_t tiles_total{0};
    uint64_t tiles_hit{0};
    uint64_t tiles_miss{0};
    uint64_t tiles_partial{0};
    uint64_t bytes_allocated_peak{0};
    uint64_t node_cache_hash_collisions{0};

    // Host & environment specs
    std::string started_at_iso;
    std::string finished_at_iso;
    std::string git_commit_short;
    std::string build_type;
    std::string compiler_info;
    std::string os;
    std::string cpu_model;
    int cores{0};
};

struct FrameTelemetryRecord {
    int frame_number{0};
    double duration_ms{0.0};
    bool cache_hit{false};
    double dirty_area_ratio{1.0};
};

struct PhaseTelemetryRecord {
    std::string phase_name;
    double duration_ms{0.0};
};

struct CounterTelemetryRecord {
    std::string counter_name;
    uint64_t counter_value{0};
};

} // namespace chronon3d::telemetry
