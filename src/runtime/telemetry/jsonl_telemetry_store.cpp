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

    j["clear_calls"] = run.clear_calls;
    j["clear_pixels"] = run.clear_pixels;
    j["composite_calls"] = run.composite_calls;
    j["composite_pixels"] = run.composite_pixels;
    j["transform_calls"] = run.transform_calls;
    j["transform_pixels"] = run.transform_pixels;
    j["effect_stack_calls"] = run.effect_stack_calls;
    j["effect_pixels"] = run.effect_pixels;
    j["layer_culling_tests"] = run.layer_culling_tests;
    j["layers_culled"] = run.layers_culled;
    j["layers_visible"] = run.layers_visible;
    j["framebuffer_allocations"] = run.framebuffer_allocations;
    j["framebuffer_reuses"] = run.framebuffer_reuses;
    j["framebuffer_bytes_allocated"] = run.framebuffer_bytes_allocated;
    j["framebuffer_bytes_peak"] = run.framebuffer_bytes_peak;
    j["dirty_rect_count"] = run.dirty_rect_count;
    j["dirty_pixels"] = run.dirty_pixels;
    j["dirty_full_fallbacks"] = run.dirty_full_fallbacks;

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

bool JsonlTelemetryStore::write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "node_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["node_name"] = ev.node_name;
        j["node_type"] = ev.node_type;
        j["layer_id"] = ev.layer_id;
        j["duration_ms"] = ev.duration_ms;
        j["cache_status"] = ev.cache_status;
        j["cache_key_digest"] = ev.cache_key_digest;
        j["input_count"] = ev.input_count;
        j["output_width"] = ev.output_width;
        j["output_height"] = ev.output_height;
        j["output_bytes"] = ev.output_bytes;
        j["bbox_x"] = ev.bbox_x;
        j["bbox_y"] = ev.bbox_y;
        j["bbox_w"] = ev.bbox_w;
        j["bbox_h"] = ev.bbox_h;
        j["visible_x"] = ev.visible_x;
        j["visible_y"] = ev.visible_y;
        j["visible_w"] = ev.visible_w;
        j["visible_h"] = ev.visible_h;
        j["pixels_touched"] = ev.pixels_touched;
        j["pixels_cleared"] = ev.pixels_cleared;
        j["pixels_composited"] = ev.pixels_composited;
        j["pixels_transformed"] = ev.pixels_transformed;
        j["pixels_blurred"] = ev.pixels_blurred;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "layer_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["layer_id"] = ev.layer_id;
        j["layer_name"] = ev.layer_name;
        j["layer_type"] = ev.layer_type;
        j["duration_ms"] = ev.duration_ms;
        j["visible"] = ev.visible;
        j["cull_reason"] = ev.cull_reason;
        j["opacity"] = ev.opacity;
        j["blend_mode"] = ev.blend_mode;
        j["bbox_x"] = ev.bbox_x;
        j["bbox_y"] = ev.bbox_y;
        j["bbox_w"] = ev.bbox_w;
        j["bbox_h"] = ev.bbox_h;
        j["visible_x"] = ev.visible_x;
        j["visible_y"] = ev.visible_y;
        j["visible_w"] = ev.visible_w;
        j["visible_h"] = ev.visible_h;
        j["area_pixels"] = ev.area_pixels;
        j["visible_pixels"] = ev.visible_pixels;
        j["dirty_pixels"] = ev.dirty_pixels;
        j["effects"] = ev.effects;
        j["effect_padding"] = ev.effect_padding;
        j["glyphs_rasterized"] = ev.glyphs_rasterized;
        j["images_sampled"] = ev.images_sampled;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "cache_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["node_name"] = ev.node_name;
        j["cacheable"] = ev.cacheable;
        j["cache_status"] = ev.cache_status;
        j["key_digest"] = ev.key_digest;
        j["params_hash"] = ev.params_hash;
        j["source_hash"] = ev.source_hash;
        j["input_hash"] = ev.input_hash;
        j["output_bytes"] = ev.output_bytes;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "culling_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["layer_id"] = ev.layer_id;
        j["visible"] = ev.visible;
        j["reason"] = ev.reason;
        j["bbox_x"] = ev.bbox_x;
        j["bbox_y"] = ev.bbox_y;
        j["bbox_w"] = ev.bbox_w;
        j["bbox_h"] = ev.bbox_h;
        j["visible_x"] = ev.visible_x;
        j["visible_y"] = ev.visible_y;
        j["visible_w"] = ev.visible_w;
        j["visible_h"] = ev.visible_h;
        j["saved_pixels"] = ev.saved_pixels;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "text_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["layer_id"] = ev.layer_id;
        j["text_length"] = ev.text_length;
        j["line_count"] = ev.line_count;
        j["glyph_count"] = ev.glyph_count;
        j["glyphs_rasterized"] = ev.glyphs_rasterized;
        j["glyph_cache_hits"] = ev.glyph_cache_hits;
        j["glyph_cache_misses"] = ev.glyph_cache_misses;
        j["layout_ms"] = ev.layout_ms;
        j["raster_ms"] = ev.raster_ms;
        j["composite_ms"] = ev.composite_ms;
        j["font_path"] = ev.font_path;
        j["font_size"] = ev.font_size;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "image_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["layer_id"] = ev.layer_id;
        j["image_path"] = ev.image_path;
        j["image_width"] = ev.image_width;
        j["image_height"] = ev.image_height;
        j["cache_status"] = ev.cache_status;
        j["decode_ms"] = ev.decode_ms;
        j["sample_ms"] = ev.sample_ms;
        j["sampled_pixels"] = ev.sampled_pixels;
        out << j.dump() << "\n";
    }
    return true;
}

bool JsonlTelemetryStore::write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_path.empty()) return false;

    std::ofstream out(m_path, std::ios::app);
    if (!out) return false;

    for (const auto& ev : events) {
        nlohmann::json j;
        j["type"] = "tile_event";
        j["run_id"] = run_id;
        j["frame_number"] = ev.frame_number;
        j["layer_id"] = ev.layer_id;
        j["tile_x"] = ev.tile_x;
        j["tile_y"] = ev.tile_y;
        j["tile_status"] = ev.tile_status;
        j["dirty_rects_count"] = ev.dirty_rects_count;
        out << j.dump() << "\n";
    }
    return true;
}

} // namespace chronon3d::telemetry
