#pragma once

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/core/trace.hpp>

#include <cstdint>
#include <string>
#include <vector>

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

namespace chronon3d::telemetry {

inline void record_render_telemetry(const RenderTelemetryRow& row) {
    detail::thread_local_buffer().push_back(row);
}

inline void record_node_telemetry(const NodeTelemetryRecord& rec) {
    detail::node_telemetry_buffer().push_back(rec);
}

inline std::vector<NodeTelemetryRecord> collect_node_telemetry() {
    auto& buf = detail::node_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<NodeTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_layer_telemetry(const LayerTelemetryRecord& rec) {
    detail::layer_telemetry_buffer().push_back(rec);
}

inline std::vector<LayerTelemetryRecord> collect_layer_telemetry() {
    auto& buf = detail::layer_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<LayerTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_cache_telemetry(const CacheTelemetryRecord& rec) {
    detail::cache_telemetry_buffer().push_back(rec);
}

inline std::vector<CacheTelemetryRecord> collect_cache_telemetry() {
    auto& buf = detail::cache_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<CacheTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_culling_telemetry(const CullingTelemetryRecord& rec) {
    detail::culling_telemetry_buffer().push_back(rec);
}

inline std::vector<CullingTelemetryRecord> collect_culling_telemetry() {
    auto& buf = detail::culling_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<CullingTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_text_telemetry(const TextTelemetryRecord& rec) {
    detail::text_telemetry_buffer().push_back(rec);
}

inline std::vector<TextTelemetryRecord> collect_text_telemetry() {
    auto& buf = detail::text_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<TextTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_image_telemetry(const ImageTelemetryRecord& rec) {
    detail::image_telemetry_buffer().push_back(rec);
}

inline std::vector<ImageTelemetryRecord> collect_image_telemetry() {
    auto& buf = detail::image_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<ImageTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void record_tile_telemetry(const TileTelemetryRecord& rec) {
    detail::tile_telemetry_buffer().push_back(rec);
}

inline std::vector<TileTelemetryRecord> collect_tile_telemetry() {
    auto& buf = detail::tile_telemetry_buffer();
    if (buf.empty()) return {};
    std::vector<TileTelemetryRecord> result = std::move(buf);
    buf.clear();
    return result;
}

inline void flush_telemetry() {
    auto& buffer = detail::thread_local_buffer();
    if (buffer.empty()) return;

    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::size_t run_id = detail::current_run_id();
    const std::filesystem::path csv_path = detail::telemetry_csv_path();

    if (csv_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(csv_path.parent_path(), ec);
    }

    std::scoped_lock lock(detail::telemetry_mutex());
    detail::migrate_legacy_csv(csv_path);

    auto& global = detail::global_rows();
    std::ofstream csv(csv_path, std::ios::app);
    if (csv) {
        detail::ensure_csv_header(csv, csv_path);
    }

    for (const auto& row : buffer) {
        if (csv) {
            csv << ts << ','
                << run_id << ','
                << detail::csv_escape(row.event) << ','
                << row.frame << ','
                << row.width << ','
                << row.height << ','
                << row.total_ms << ','
                << row.setup_ms << ','
                << row.composite_ms << ','
                << row.blur_ms << ','
                << row.encode_ms << ','
                << row.ram_mb << ','
                << row.cache_hit << ','
                << row.layer_count << ','
                << row.cache_hits << ','
                << row.cache_misses << ','
                << row.nodes_executed << ','
                << row.clear_calls << ','
                << row.clear_pixels << ','
                << row.composite_calls << ','
                << row.composite_pixels << ','
                << row.transform_calls << ','
                << row.transform_pixels << ','
                << row.effect_stack_calls << ','
                << row.effect_pixels << ','
                << row.text_glyphs_rasterized << ','
                << row.framebuffer_allocations << ','
                << row.framebuffer_reuses << ','
                << row.dirty_full_fallbacks << ','
                << row.dirty_full_fallback_predicted_bounds_missing << ','
                << row.dirty_full_fallback_composite_missing_input_bounds << ','
                << row.dirty_full_fallback_transform_bounds_unknown << ','
                << row.dirty_full_fallback_effect_bounds_unknown << '\n';
        }
        global.push_back(row);
        if (global.size() > 1000) {
            global.erase(global.begin());
        }
    }

    buffer.clear();
    detail::write_summary_file(global);
}

} // namespace chronon3d::telemetry
