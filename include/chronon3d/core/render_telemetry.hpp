#pragma once

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/core/trace.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace chronon3d::telemetry {

struct RenderTelemetryRow {
    std::string event;
    chronon3d::Frame frame{0};
    int width{0};
    int height{0};
    double total_ms{0.0};
    double setup_ms{0.0};
    double composite_ms{0.0};
    double blur_ms{0.0};
    double encode_ms{0.0};
    double ram_mb{0.0};
    int cache_hit{0};
    int layer_count{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t nodes_executed{0};
    uint64_t clear_calls{0};
    uint64_t clear_pixels{0};
    uint64_t composite_calls{0};
    uint64_t composite_pixels{0};
    uint64_t transform_calls{0};
    uint64_t transform_pixels{0};
    uint64_t effect_stack_calls{0};
    uint64_t effect_pixels{0};
    uint64_t text_glyphs_rasterized{0};
    uint64_t framebuffer_allocations{0};
    uint64_t framebuffer_reuses{0};
    uint64_t dirty_full_fallbacks{0};
    uint64_t dirty_full_fallback_predicted_bounds_missing{0};
    uint64_t dirty_full_fallback_composite_missing_input_bounds{0};
    uint64_t dirty_full_fallback_transform_bounds_unknown{0};
    uint64_t dirty_full_fallback_effect_bounds_unknown{0};
};

} // namespace chronon3d::telemetry

#include <chronon3d/core/render_telemetry_detail.hpp>

inline std::vector<RenderTelemetryRow>& global_rows() {
    static std::vector<RenderTelemetryRow> rows;
    return rows;
}

inline std::vector<RenderTelemetryRow>& thread_local_buffer() {
    static thread_local std::vector<RenderTelemetryRow> buffer;
    return buffer;
}

inline std::filesystem::path telemetry_csv_path() {
    if (const char* env = std::getenv("CHRONON_RENDER_LOG"); env && *env) {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("output") / "render_telemetry.csv";
}

inline std::filesystem::path telemetry_summary_path() {
    auto path = telemetry_csv_path();
    return path.replace_filename("render_summary.txt");
}

inline std::string csv_escape(std::string_view value) {
    const bool needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(value);
    }

    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void ensure_csv_header(std::ofstream& out, const std::filesystem::path& path);

bool csv_header_matches(const std::filesystem::path& path);

void migrate_legacy_csv(const std::filesystem::path& path);

std::size_t read_last_run_id(const std::filesystem::path& path);

std::size_t current_run_id();

void write_summary_file(const std::vector<RenderTelemetryRow>& rows);
} // namespace detail

inline void record_render_telemetry(const RenderTelemetryRow& row) {
    detail::thread_local_buffer().push_back(row);
}

inline void record_node_telemetry(const NodeTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::node_telemetry_mutex());
    detail::node_telemetry_store().push_back(rec);
}

inline std::vector<NodeTelemetryRecord> collect_node_telemetry() {
    std::lock_guard<std::mutex> lock(detail::node_telemetry_mutex());
    auto& buf = detail::node_telemetry_store();
    if (buf.empty()) return {};
    std::vector<NodeTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_layer_telemetry(const LayerTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::layer_telemetry_mutex());
    detail::layer_telemetry_store().push_back(rec);
}

inline std::vector<LayerTelemetryRecord> collect_layer_telemetry() {
    std::lock_guard<std::mutex> lock(detail::layer_telemetry_mutex());
    auto& buf = detail::layer_telemetry_store();
    if (buf.empty()) return {};
    std::vector<LayerTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_cache_telemetry(const CacheTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::cache_telemetry_mutex());
    detail::cache_telemetry_store().push_back(rec);
}

inline std::vector<CacheTelemetryRecord> collect_cache_telemetry() {
    std::lock_guard<std::mutex> lock(detail::cache_telemetry_mutex());
    auto& buf = detail::cache_telemetry_store();
    if (buf.empty()) return {};
    std::vector<CacheTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_culling_telemetry(const CullingTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::culling_telemetry_mutex());
    detail::culling_telemetry_store().push_back(rec);
}

inline std::vector<CullingTelemetryRecord> collect_culling_telemetry() {
    std::lock_guard<std::mutex> lock(detail::culling_telemetry_mutex());
    auto& buf = detail::culling_telemetry_store();
    if (buf.empty()) return {};
    std::vector<CullingTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_text_telemetry(const TextTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::text_telemetry_mutex());
    detail::text_telemetry_store().push_back(rec);
}

inline std::vector<TextTelemetryRecord> collect_text_telemetry() {
    std::lock_guard<std::mutex> lock(detail::text_telemetry_mutex());
    auto& buf = detail::text_telemetry_store();
    if (buf.empty()) return {};
    std::vector<TextTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_image_telemetry(const ImageTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::image_telemetry_mutex());
    detail::image_telemetry_store().push_back(rec);
}

inline std::vector<ImageTelemetryRecord> collect_image_telemetry() {
    std::lock_guard<std::mutex> lock(detail::image_telemetry_mutex());
    auto& buf = detail::image_telemetry_store();
    if (buf.empty()) return {};
    std::vector<ImageTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_tile_telemetry(const TileTelemetryRecord& rec) {
    std::lock_guard<std::mutex> lock(detail::tile_telemetry_mutex());
    detail::tile_telemetry_store().push_back(rec);
}

inline std::vector<TileTelemetryRecord> collect_tile_telemetry() {
    std::lock_guard<std::mutex> lock(detail::tile_telemetry_mutex());
    auto& buf = detail::tile_telemetry_store();
    if (buf.empty()) return {};
    std::vector<TileTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

void flush_telemetry();

} // namespace chronon3d::telemetry
