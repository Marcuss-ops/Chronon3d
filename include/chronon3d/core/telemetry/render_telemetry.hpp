#pragma once
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <mutex>
#include <vector>

namespace chronon3d::telemetry {

namespace detail {

inline std::mutex& node_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<NodeTelemetryRecord>& node_telemetry_store() {
    static std::vector<NodeTelemetryRecord> store;
    return store;
}

inline std::mutex& layer_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<LayerTelemetryRecord>& layer_telemetry_store() {
    static std::vector<LayerTelemetryRecord> store;
    return store;
}

inline std::mutex& cache_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<CacheTelemetryRecord>& cache_telemetry_store() {
    static std::vector<CacheTelemetryRecord> store;
    return store;
}

inline std::mutex& culling_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<CullingTelemetryRecord>& culling_telemetry_store() {
    static std::vector<CullingTelemetryRecord> store;
    return store;
}

inline std::mutex& text_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<TextTelemetryRecord>& text_telemetry_store() {
    static std::vector<TextTelemetryRecord> store;
    return store;
}

inline std::mutex& image_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<ImageTelemetryRecord>& image_telemetry_store() {
    static std::vector<ImageTelemetryRecord> store;
    return store;
}

inline std::mutex& tile_telemetry_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<TileTelemetryRecord>& tile_telemetry_store() {
    static std::vector<TileTelemetryRecord> store;
    return store;
}

} // namespace detail

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

} // namespace chronon3d::telemetry
