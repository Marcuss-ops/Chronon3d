#pragma once

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/core/trace.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <thread>

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

namespace detail {

inline std::mutex& telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

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

inline void ensure_csv_header(std::ofstream& out, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        out << "ts,run_id,event,frame,w,h,total_ms,setup_ms,composite_ms,blur_ms,encode_ms,ram_mb,cache_hit,layer_count,"
               "cache_hits,cache_misses,nodes_executed,clear_calls,clear_pixels,composite_calls,composite_pixels,"
               "transform_calls,transform_pixels,effect_stack_calls,effect_pixels,text_glyphs_rasterized,"
               "framebuffer_allocations,framebuffer_reuses,dirty_full_fallbacks,"
               "dirty_full_fallback_predicted_bounds_missing,dirty_full_fallback_composite_missing_input_bounds,"
               "dirty_full_fallback_transform_bounds_unknown,dirty_full_fallback_effect_bounds_unknown\n";
    }
}

inline bool csv_header_matches(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        return true;
    }

    std::ifstream in(path);
    std::string header;
    if (!std::getline(in, header)) {
        return true;
    }
    return header == "ts,run_id,event,frame,w,h,total_ms,setup_ms,composite_ms,blur_ms,encode_ms,ram_mb,cache_hit,layer_count,cache_hits,cache_misses,nodes_executed,clear_calls,clear_pixels,composite_calls,composite_pixels,transform_calls,transform_pixels,effect_stack_calls,effect_pixels,text_glyphs_rasterized,framebuffer_allocations,framebuffer_reuses,dirty_full_fallbacks,dirty_full_fallback_predicted_bounds_missing,dirty_full_fallback_composite_missing_input_bounds,dirty_full_fallback_transform_bounds_unknown,dirty_full_fallback_effect_bounds_unknown";
}

inline void migrate_legacy_csv(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return;
    }

    if (csv_header_matches(path)) {
        return;
    }

    std::error_code ec;
    const auto legacy = path.parent_path() / std::filesystem::path(
        path.stem().string() + ".legacy" + path.extension().string());
    std::filesystem::remove(legacy, ec);
    std::filesystem::rename(path, legacy, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
    }
}

inline std::size_t read_last_run_id(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return 0;
    }

    std::string line;
    std::size_t max_id = 0;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (first_line) {
            first_line = false;
            continue;
        }
        const auto first = line.find(',');
        if (first == std::string::npos) continue;
        const auto second = line.find(',', first + 1);
        if (second == std::string::npos) continue;
        const std::string run_id_str = line.substr(first + 1, second - first - 1);
        try {
            max_id = std::max(max_id, static_cast<std::size_t>(std::stoull(run_id_str)));
        } catch (...) {
        }
    }
    return max_id;
}

inline std::size_t current_run_id() {
    static const std::size_t run_id = [] {
        const auto path = telemetry_csv_path();
        return read_last_run_id(path) + 1;
    }();
    return run_id;
}

inline double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    p = std::clamp(p, 0.0, 1.0);
    const size_t index = static_cast<size_t>(std::floor(p * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

inline std::string format_ms(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

inline std::string format_count(double value) {
    std::ostringstream oss;
    oss << static_cast<uint64_t>(std::llround(value));
    return oss.str();
}

inline void write_summary_file(const std::vector<RenderTelemetryRow>& rows) {
    const auto path = telemetry_summary_path();
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }

    out << "Chronon3D render summary\n";
    out << "total_rows=" << rows.size() << "\n";

    if (rows.empty()) {
        out << "no render samples\n";
        return;
    }

    const std::size_t total_samples = rows.size();
    const std::size_t cache_hits = static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [](const RenderTelemetryRow& row) {
            return row.cache_hit != 0;
        }));
    const double cache_hit_rate = total_samples > 0
        ? static_cast<double>(cache_hits) / static_cast<double>(total_samples)
        : 0.0;
    const double avg_layer_count = [&rows]() {
        if (rows.empty()) {
            return 0.0;
        }
        double sum = 0.0;
        for (const auto& row : rows) {
            sum += static_cast<double>(row.layer_count);
        }
        return sum / static_cast<double>(rows.size());
    }();

    struct Series {
        std::vector<double> total;
        std::vector<double> setup;
        std::vector<double> composite;
        std::vector<double> blur;
        std::vector<double> encode;
        std::vector<double> ram;
        std::vector<double> cache_hits;
        std::vector<double> cache_misses;
        std::vector<double> nodes_executed;
        std::vector<double> clear_calls;
        std::vector<double> clear_pixels;
        std::vector<double> composite_calls;
        std::vector<double> composite_pixels;
        std::vector<double> transform_calls;
        std::vector<double> transform_pixels;
        std::vector<double> effect_stack_calls;
        std::vector<double> effect_pixels;
        std::vector<double> text_glyphs_rasterized;
        std::vector<double> framebuffer_allocations;
        std::vector<double> framebuffer_reuses;
        std::vector<double> dirty_full_fallbacks;
        std::vector<double> dirty_full_fallback_predicted_bounds_missing;
        std::vector<double> dirty_full_fallback_composite_missing_input_bounds;
        std::vector<double> dirty_full_fallback_transform_bounds_unknown;
        std::vector<double> dirty_full_fallback_effect_bounds_unknown;
    };

    std::map<std::string, Series> grouped;
    for (const auto& row : rows) {
        auto& s = grouped[row.event];
        s.total.push_back(row.total_ms);
        s.setup.push_back(row.setup_ms);
        s.composite.push_back(row.composite_ms);
        s.blur.push_back(row.blur_ms);
        s.encode.push_back(row.encode_ms);
        s.ram.push_back(row.ram_mb);
        s.cache_hits.push_back(static_cast<double>(row.cache_hits));
        s.cache_misses.push_back(static_cast<double>(row.cache_misses));
        s.nodes_executed.push_back(static_cast<double>(row.nodes_executed));
        s.clear_calls.push_back(static_cast<double>(row.clear_calls));
        s.clear_pixels.push_back(static_cast<double>(row.clear_pixels));
        s.composite_calls.push_back(static_cast<double>(row.composite_calls));
        s.composite_pixels.push_back(static_cast<double>(row.composite_pixels));
        s.transform_calls.push_back(static_cast<double>(row.transform_calls));
        s.transform_pixels.push_back(static_cast<double>(row.transform_pixels));
        s.effect_stack_calls.push_back(static_cast<double>(row.effect_stack_calls));
        s.effect_pixels.push_back(static_cast<double>(row.effect_pixels));
        s.text_glyphs_rasterized.push_back(static_cast<double>(row.text_glyphs_rasterized));
        s.framebuffer_allocations.push_back(static_cast<double>(row.framebuffer_allocations));
        s.framebuffer_reuses.push_back(static_cast<double>(row.framebuffer_reuses));
        s.dirty_full_fallbacks.push_back(static_cast<double>(row.dirty_full_fallbacks));
        s.dirty_full_fallback_predicted_bounds_missing.push_back(static_cast<double>(row.dirty_full_fallback_predicted_bounds_missing));
        s.dirty_full_fallback_composite_missing_input_bounds.push_back(static_cast<double>(row.dirty_full_fallback_composite_missing_input_bounds));
        s.dirty_full_fallback_transform_bounds_unknown.push_back(static_cast<double>(row.dirty_full_fallback_transform_bounds_unknown));
        s.dirty_full_fallback_effect_bounds_unknown.push_back(static_cast<double>(row.dirty_full_fallback_effect_bounds_unknown));
    }

    auto max_value = [](const std::vector<double>& values) {
        return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    };

    for (const auto& [event, s] : grouped) {
        out << "event=" << event << " count=" << s.total.size()
            << " total_p50_ms=" << format_ms(percentile(s.total, 0.50))
            << " total_p95_ms=" << format_ms(percentile(s.total, 0.95))
            << " setup_p95_ms=" << format_ms(percentile(s.setup, 0.95))
            << " composite_p95_ms=" << format_ms(percentile(s.composite, 0.95))
            << " blur_p95_ms=" << format_ms(percentile(s.blur, 0.95))
            << " encode_p95_ms=" << format_ms(percentile(s.encode, 0.95))
            << " ram_p95_mb=" << format_ms(percentile(s.ram, 0.95))
            << " cache_hits_p95=" << format_ms(percentile(s.cache_hits, 0.95))
            << " cache_misses_p95=" << format_ms(percentile(s.cache_misses, 0.95))
            << " nodes_executed_p95=" << format_ms(percentile(s.nodes_executed, 0.95))
            << " clear_calls_p95=" << format_ms(percentile(s.clear_calls, 0.95))
            << " clear_pixels_p95=" << format_ms(percentile(s.clear_pixels, 0.95))
            << " composite_calls_p95=" << format_ms(percentile(s.composite_calls, 0.95))
            << " composite_pixels_p95=" << format_ms(percentile(s.composite_pixels, 0.95))
            << " transform_calls_p95=" << format_ms(percentile(s.transform_calls, 0.95))
            << " transform_pixels_p95=" << format_ms(percentile(s.transform_pixels, 0.95))
            << " effect_stack_calls_p95=" << format_ms(percentile(s.effect_stack_calls, 0.95))
            << " effect_pixels_p95=" << format_ms(percentile(s.effect_pixels, 0.95))
            << " text_glyphs_rasterized_p95=" << format_ms(percentile(s.text_glyphs_rasterized, 0.95))
            << " framebuffer_allocations_p95=" << format_ms(percentile(s.framebuffer_allocations, 0.95))
            << " framebuffer_reuses_p95=" << format_ms(percentile(s.framebuffer_reuses, 0.95))
            << " dirty_full_fallbacks_total=" << format_count(max_value(s.dirty_full_fallbacks))
            << " dirty_full_fallback_predicted_bounds_missing_total=" << format_count(max_value(s.dirty_full_fallback_predicted_bounds_missing))
            << " dirty_full_fallback_composite_missing_input_bounds_total=" << format_count(max_value(s.dirty_full_fallback_composite_missing_input_bounds))
            << " dirty_full_fallback_transform_bounds_unknown_total=" << format_count(max_value(s.dirty_full_fallback_transform_bounds_unknown))
            << " dirty_full_fallback_effect_bounds_unknown_total=" << format_count(max_value(s.dirty_full_fallback_effect_bounds_unknown))
            << "\n";
    }

    const auto bottleneck_event = [&grouped]() {
        std::string best_event = "unknown";
        double best_ms = -1.0;
        for (const auto& [event, s] : grouped) {
            const double total_p95 = percentile(s.total, 0.95);
            if (total_p95 > best_ms) {
                best_ms = total_p95;
                best_event = event;
            }
        }
        return std::pair{best_event, best_ms};
    }();

    out << "overview "
        << "bottleneck=" << bottleneck_event.first << ' '
        << "layer_count=" << format_ms(avg_layer_count) << ' '
        << "cache_hit_rate=" << format_ms(cache_hit_rate * 100.0) << "% "
        << "samples=" << total_samples
        << "\n";
}

} // namespace detail

inline void record_render_telemetry(const RenderTelemetryRow& row) {
    detail::thread_local_buffer().push_back(row);
}

// ── Per-node telemetry accumulator (thread_local, cleared per frame) ───────────
// Populated during graph execution, flushed via collect_node_telemetry().

namespace detail {
inline std::vector<NodeTelemetryRecord>& node_telemetry_buffer() {
    static thread_local std::vector<NodeTelemetryRecord> buf;
    return buf;
}
inline std::vector<LayerTelemetryRecord>& layer_telemetry_buffer() {
    static thread_local std::vector<LayerTelemetryRecord> buf;
    return buf;
}
inline std::vector<CacheTelemetryRecord>& cache_telemetry_buffer() {
    static thread_local std::vector<CacheTelemetryRecord> buf;
    return buf;
}
inline std::vector<CullingTelemetryRecord>& culling_telemetry_buffer() {
    static thread_local std::vector<CullingTelemetryRecord> buf;
    return buf;
}
inline std::vector<TextTelemetryRecord>& text_telemetry_buffer() {
    static thread_local std::vector<TextTelemetryRecord> buf;
    return buf;
}
inline std::vector<ImageTelemetryRecord>& image_telemetry_buffer() {
    static thread_local std::vector<ImageTelemetryRecord> buf;
    return buf;
}
inline std::vector<TileTelemetryRecord>& tile_telemetry_buffer() {
    static thread_local std::vector<TileTelemetryRecord> buf;
    return buf;
}
} // namespace detail

inline void record_node_telemetry(const NodeTelemetryRecord& rec) {
    detail::node_telemetry_buffer().push_back(rec);
}

// Returns collected node telemetry and clears the buffer.
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

// Returns collected layer telemetry and clears the buffer.
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
