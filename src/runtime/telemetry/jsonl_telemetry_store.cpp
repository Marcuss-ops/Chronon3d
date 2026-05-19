#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <nlohmann/json.hpp>
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

    nlohmann::json j;
    j["type"] = "run";
    j["run_id"] = run.run_id;
    j["composition_id"] = run.composition_id;
    j["output_path"] = run.output_path;
    j["success"] = run.success;
    j["error_code"] = run.error_code;
    j["error_message"] = run.error_message;
    j["frames_total"] = run.frames_total;
    j["frames_written"] = run.frames_written;
    j["wall_time_ms"] = run.wall_time_ms;
    j["render_ms"] = run.render_ms;
    j["encode_ms"] = run.encode_ms;
    j["effective_fps"] = run.effective_fps;

    // Performance counters
    j["pixels_touched"] = run.pixels_touched;
    j["cache_hits"] = run.cache_hits;
    j["cache_misses"] = run.cache_misses;
    j["nodes_executed"] = run.nodes_executed;
    j["layers_rendered"] = run.layers_rendered;
    j["text_glyphs_rasterized"] = run.text_glyphs_rasterized;
    j["images_sampled"] = run.images_sampled;
    j["blur_pixels"] = run.blur_pixels;
    j["simd_lerp_calls"] = run.simd_lerp_calls;
    j["tiles_total"] = run.tiles_total;
    j["tiles_hit"] = run.tiles_hit;
    j["tiles_miss"] = run.tiles_miss;
    j["tiles_partial"] = run.tiles_partial;
    j["bytes_allocated_peak"] = run.bytes_allocated_peak;
    j["node_cache_hash_collisions"] = run.node_cache_hash_collisions;

    // Host & Environment specs
    j["started_at_iso"] = run.started_at_iso;
    j["finished_at_iso"] = run.finished_at_iso;
    j["git_commit_short"] = run.git_commit_short;
    j["build_type"] = run.build_type;
    j["compiler_info"] = run.compiler_info;
    j["os"] = run.os;
    j["cpu_model"] = run.cpu_model;
    j["cores"] = run.cores;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;
    out << j.dump() << "\n";
    return true;
}

bool JsonlTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& frame : frames) {
        nlohmann::json j;
        j["type"] = "frame";
        j["run_id"] = run_id;
        j["frame_number"] = frame.frame_number;
        j["duration_ms"] = frame.duration_ms;
        j["cache_hit"] = frame.cache_hit;
        j["dirty_area_ratio"] = frame.dirty_area_ratio;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& phase : phases) {
        nlohmann::json j;
        j["type"] = "phase";
        j["run_id"] = run_id;
        j["phase_name"] = phase.phase_name;
        j["duration_ms"] = phase.duration_ms;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& cnt : counters) {
        nlohmann::json j;
        j["type"] = "counter";
        j["run_id"] = run_id;
        j["counter_name"] = cnt.counter_name;
        j["counter_value"] = cnt.counter_value;
        out << j.dump() << "\n";
    }
    return true;
}

} // namespace chronon3d::telemetry
