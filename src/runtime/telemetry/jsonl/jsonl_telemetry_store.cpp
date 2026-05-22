#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include "jsonl_serializers.hpp"
#include <fstream>
#include <filesystem>

namespace chronon3d::telemetry {

bool JsonlTelemetryStore::initialize(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_path = path;
    
    std::filesystem::path fs_path(m_path);
    if (fs_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }
    return true;
}

bool JsonlTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;
    out << serialize_run(run).dump() << "\n";
    return true;
}

bool JsonlTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& frame : frames) {
        out << serialize_frame(run_id, frame).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& phase : phases) {
        out << serialize_phase(run_id, phase).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& cnt : counters) {
        out << serialize_counter(run_id, cnt).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_node_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_layer_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_cache_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_culling_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_text_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_image_event(run_id, ev).dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        out << serialize_tile_event(run_id, ev).dump() << "\n";
    }
    return true;
}

} // namespace chronon3d::telemetry
