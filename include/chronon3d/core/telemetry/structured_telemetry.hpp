#pragma once

#include <chronon3d/core/profiling/trace.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

struct sqlite3;

namespace chronon3d::telemetry {

class StructuredTelemetry {
public:
    StructuredTelemetry();
    ~StructuredTelemetry();

    bool open(const std::filesystem::path& path);
    void close();

    void record_run(const std::string& run_id, const std::string& comp_id);
    void record_events(const std::string& run_id, const std::vector<TraceEvent>& events);
    void record_counters(const std::string& run_id, int frame, const RenderCounters& counters);

    // High-level record functions as proposed by user
    void record_node_event(const std::string& run_id, const TraceEvent& event);
    void record_layer_event(const std::string& run_id, const TraceEvent& event);
    void record_cache_event(const std::string& run_id, const TraceEvent& event);

    struct TextEvent {
        std::string layer_id;
        int text_length{0}, line_count{0}, glyph_count{0}, glyphs_rasterized{0};
        int glyph_cache_hits{0}, glyph_cache_misses{0};
        double layout_ms{0}, raster_ms{0}, composite_ms{0}, font_size{0};
        std::string font_path;
    };
    void record_text_event(const std::string& run_id, int frame, const TextEvent& e);

    struct ImageEvent {
        std::string layer_id, image_path, cache_status;
        int width{0}, height{0}, sampled_pixels{0};
        double decode_ms{0}, sample_ms{0};
    };
    void record_image_event(const std::string& run_id, int frame, const ImageEvent& e);

    struct CullingEvent {
        std::string layer_id, reason;
        int visible{0}, saved_pixels{0};
        float bbox_x{0}, bbox_y{0}, bbox_w{0}, bbox_h{0};
        float visible_x{0}, visible_y{0}, visible_w{0}, visible_h{0};
    };
    void record_culling_event(const std::string& run_id, int frame, const CullingEvent& e);

    struct RunUpdate {
        int success{1}, frames_total{0}, frames_written{0};
        double wall_time_ms{0}, render_ms{0}, effective_fps{0};
        std::string output_path;
    };
    void update_run(const std::string& run_id, const RunUpdate& update);

private:
    bool init_db();
    void exec_sql(const std::string& sql);

    sqlite3* db_{nullptr};
    std::string current_run_id_;
};

} // namespace chronon3d::telemetry
