#include <chronon3d/core/telemetry/render_telemetry_detail.hpp>

// Full type definitions needed by the implementation
#include <chronon3d/core/telemetry/render_telemetry.hpp>

#include <algorithm>
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

namespace chronon3d::telemetry::detail {

// -----------------------------------------------------------------------
// Mutex getters
// -----------------------------------------------------------------------

std::mutex& telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& node_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& layer_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& cache_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& culling_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& text_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& image_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& tile_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

// -----------------------------------------------------------------------
// Global / thread-local stores
// -----------------------------------------------------------------------

std::vector<RenderTelemetryRow>& global_rows() {
    static std::vector<RenderTelemetryRow> rows;
    return rows;
}

std::vector<RenderTelemetryRow>& thread_local_buffer() {
    static thread_local std::vector<RenderTelemetryRow> buffer;
    return buffer;
}

std::vector<NodeTelemetryRecord>& node_telemetry_store() {
    static std::vector<NodeTelemetryRecord> store;
    return store;
}

std::vector<LayerTelemetryRecord>& layer_telemetry_store() {
    static std::vector<LayerTelemetryRecord> store;
    return store;
}

std::vector<CacheTelemetryRecord>& cache_telemetry_store() {
    static std::vector<CacheTelemetryRecord> store;
    return store;
}

std::vector<CullingTelemetryRecord>& culling_telemetry_store() {
    static std::vector<CullingTelemetryRecord> store;
    return store;
}

std::vector<TextTelemetryRecord>& text_telemetry_store() {
    static std::vector<TextTelemetryRecord> store;
    return store;
}

std::vector<ImageTelemetryRecord>& image_telemetry_store() {
    static std::vector<ImageTelemetryRecord> store;
    return store;
}

std::vector<TileTelemetryRecord>& tile_telemetry_store() {
    static std::vector<TileTelemetryRecord> store;
    return store;
}

// -----------------------------------------------------------------------
// CSV path helpers
// -----------------------------------------------------------------------

std::filesystem::path telemetry_csv_path() {
    if (const char* env = std::getenv("CHRONON_RENDER_LOG"); env && *env) {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("output") / "render_telemetry.csv";
}

std::filesystem::path telemetry_summary_path() {
    auto path = telemetry_csv_path();
    return path.replace_filename("render_summary.txt");
}

// -----------------------------------------------------------------------
// CSV I/O
// -----------------------------------------------------------------------

void ensure_csv_header(std::ofstream& out, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        out << "ts,run_id,event,frame,w,h,total_ms,setup_ms,composite_ms,blur_ms,encode_ms,ram_mb,cache_hit,layer_count,"
               "cache_hits,cache_misses,nodes_executed,clear_calls,clear_pixels,composite_calls,composite_pixels,"
               "transform_calls,transform_pixels,effect_stack_calls,effect_pixels,text_glyphs_rasterized,"
               "framebuffer_allocations,framebuffer_reuses,dirty_full_fallbacks,"
               "dirty_full_fallback_predicted_bounds_missing,dirty_full_fallback_composite_missing_input_bounds,"
               "dirty_full_fallback_transform_bounds_unknown,dirty_full_fallback_effect_bounds_unknown\n";
    }
}

bool csv_header_matches(const std::filesystem::path& path) {
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

void migrate_legacy_csv(const std::filesystem::path& path) {
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

std::size_t read_last_run_id(const std::filesystem::path& path) {
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

std::size_t current_run_id() {
    static const std::size_t run_id = [] {
        const auto path = telemetry_csv_path();
        return read_last_run_id(path) + 1;
    }();
    return run_id;
}

// -----------------------------------------------------------------------
// Summary file
// -----------------------------------------------------------------------

void write_summary_file(const std::vector<RenderTelemetryRow>& rows) {
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

} // namespace chronon3d::telemetry::detail
