#include <chronon3d/core/telemetry/render_telemetry_detail.hpp>

// Full type definitions needed by the implementation
#include <chronon3d/core/telemetry/render_telemetry.hpp>

#include <algorithm>
#include <array>
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

namespace {

constexpr std::array<std::string_view, 69> kCsvColumns = {{
    "ts",
    "run_id",
    "event",
    "frame",
    "w",
    "h",
    "total_ms",
    "setup_ms",
    "composite_ms",
    "blur_ms",
    "encode_ms",
    "ram_mb",
    "cache_hit",
    "layer_count",
    "cache_hits",
    "cache_misses",
    "nodes_executed",
    "clear_skipped_calls",
    "clear_skipped_pixels",
    "clear_calls",
    "clear_pixels",
    "composite_calls",
    "composite_pixels",
    "transform_calls",
    "transform_pixels",
    "effect_stack_calls",
    "effect_pixels",
    "text_glyphs_rasterized",
    "framebuffer_allocations",
    "framebuffer_reuses",
    "dirty_full_fallbacks",
    "dirty_full_fallback_predicted_bounds_missing",
    "dirty_full_fallback_composite_missing_input_bounds",
    "dirty_full_fallback_transform_bounds_unknown",
    "dirty_full_fallback_effect_bounds_unknown",
    "framebuffer_acquire_ms",
    "framebuffer_clear_ms",
    "clearnode_ms",
    "framebuffer_pool_clear_ms",
    "framebuffer_enqueue_ms",
    "framebuffer_pool_miss_count_size_mismatch",
    "framebuffer_pool_miss_count_empty",
    "framebuffer_pool_hits",
    "framebuffer_buffer_returned_to_pool_count",
    "unaligned_memory_copies",
    "frame_conversion_copy_ms",
    "video_graph_eval_ms",
    "video_conversion_ms",
    "video_pipe_write_ms",
    "video_ffmpeg_latency_ms",
    "io_queue_push_blocked_ms",
    "io_queue_pop_wait_ms",
    "io_queue_peak_depth",
    "ffmpeg_pipe_write_blocked_ms",
    "converted_frame_cache_hits",
    "ffmpeg_flush_ms",
    "io_queue_peak_bytes",
    "setup_graph_parsing_ms",
    "setup_asset_io_load_ms",
    "setup_pool_preallocation_ms",
    "image_decode_ms",
    "process_context_switches_voluntary",
    "process_context_switches_involuntary",
    "os_page_faults_major",
    "os_page_faults_minor",
    "ffmpeg_cpu_user_pct",
    "ffmpeg_cpu_sys_pct",
    "llc_references",
    "llc_misses",
}};

const std::string& csv_header_line() {
    static const std::string header = [] {
        std::string out;
        for (std::size_t i = 0; i < kCsvColumns.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            out.append(kCsvColumns[i]);
        }
        return out;
    }();
    return header;
}

template <typename T>
void append_metric(std::vector<double>& values, T value) {
    values.push_back(static_cast<double>(value));
}

} // namespace

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
        out << csv_header_line() << '\n';
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
    return header == csv_header_line();
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

    enum class Metric {
        total,
        setup,
        composite,
        blur,
        encode,
        ram,
        cache_hits,
        cache_misses,
        nodes_executed,
        clear_skipped_calls,
        clear_skipped_pixels,
        clear_calls,
        clear_pixels,
        composite_calls,
        composite_pixels,
        transform_calls,
        transform_pixels,
        effect_stack_calls,
        effect_pixels,
        text_glyphs_rasterized,
        framebuffer_allocations,
        framebuffer_reuses,
        dirty_full_fallbacks,
        dirty_full_fallback_predicted_bounds_missing,
        dirty_full_fallback_composite_missing_input_bounds,
        dirty_full_fallback_transform_bounds_unknown,
        dirty_full_fallback_effect_bounds_unknown,
        count
    };

    struct Series {
        std::array<std::vector<double>, static_cast<std::size_t>(Metric::count)> values;

        std::vector<double>& operator[](Metric metric) {
            return values[static_cast<std::size_t>(metric)];
        }

        const std::vector<double>& operator[](Metric metric) const {
            return values[static_cast<std::size_t>(metric)];
        }

        void add(const RenderTelemetryRow& row) {
            append_metric((*this)[Metric::total], row.total_ms);
            append_metric((*this)[Metric::setup], row.setup_ms);
            append_metric((*this)[Metric::composite], row.composite_ms);
            append_metric((*this)[Metric::blur], row.blur_ms);
            append_metric((*this)[Metric::encode], row.encode_ms);
            append_metric((*this)[Metric::ram], row.ram_mb);
            append_metric((*this)[Metric::cache_hits], row.cache_hits);
            append_metric((*this)[Metric::cache_misses], row.cache_misses);
            append_metric((*this)[Metric::nodes_executed], row.nodes_executed);
            append_metric((*this)[Metric::clear_skipped_calls], row.clear_skipped_calls);
            append_metric((*this)[Metric::clear_skipped_pixels], row.clear_skipped_pixels);
            append_metric((*this)[Metric::clear_calls], row.clear_calls);
            append_metric((*this)[Metric::clear_pixels], row.clear_pixels);
            append_metric((*this)[Metric::composite_calls], row.composite_calls);
            append_metric((*this)[Metric::composite_pixels], row.composite_pixels);
            append_metric((*this)[Metric::transform_calls], row.transform_calls);
            append_metric((*this)[Metric::transform_pixels], row.transform_pixels);
            append_metric((*this)[Metric::effect_stack_calls], row.effect_stack_calls);
            append_metric((*this)[Metric::effect_pixels], row.effect_pixels);
            append_metric((*this)[Metric::text_glyphs_rasterized], row.text_glyphs_rasterized);
            append_metric((*this)[Metric::framebuffer_allocations], row.framebuffer_allocations);
            append_metric((*this)[Metric::framebuffer_reuses], row.framebuffer_reuses);
            append_metric((*this)[Metric::dirty_full_fallbacks], row.dirty_full_fallbacks);
            append_metric((*this)[Metric::dirty_full_fallback_predicted_bounds_missing], row.dirty_full_fallback_predicted_bounds_missing);
            append_metric((*this)[Metric::dirty_full_fallback_composite_missing_input_bounds], row.dirty_full_fallback_composite_missing_input_bounds);
            append_metric((*this)[Metric::dirty_full_fallback_transform_bounds_unknown], row.dirty_full_fallback_transform_bounds_unknown);
            append_metric((*this)[Metric::dirty_full_fallback_effect_bounds_unknown], row.dirty_full_fallback_effect_bounds_unknown);
        }
    };

    std::map<std::string, Series> grouped;
    for (const auto& row : rows) {
        grouped[row.event].add(row);
    }

    auto max_value = [](const std::vector<double>& values) {
        return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    };

    for (const auto& [event, s] : grouped) {
        const auto& total = s[Metric::total];
        const auto& setup = s[Metric::setup];
        const auto& composite = s[Metric::composite];
        const auto& blur = s[Metric::blur];
        const auto& encode = s[Metric::encode];
        const auto& ram = s[Metric::ram];
        const auto& cache_hits = s[Metric::cache_hits];
        const auto& cache_misses = s[Metric::cache_misses];
        const auto& nodes_executed = s[Metric::nodes_executed];
        const auto& clear_skipped_calls = s[Metric::clear_skipped_calls];
        const auto& clear_skipped_pixels = s[Metric::clear_skipped_pixels];
        const auto& clear_calls = s[Metric::clear_calls];
        const auto& clear_pixels = s[Metric::clear_pixels];
        const auto& composite_calls = s[Metric::composite_calls];
        const auto& composite_pixels = s[Metric::composite_pixels];
        const auto& transform_calls = s[Metric::transform_calls];
        const auto& transform_pixels = s[Metric::transform_pixels];
        const auto& effect_stack_calls = s[Metric::effect_stack_calls];
        const auto& effect_pixels = s[Metric::effect_pixels];
        const auto& text_glyphs_rasterized = s[Metric::text_glyphs_rasterized];
        const auto& framebuffer_allocations = s[Metric::framebuffer_allocations];
        const auto& framebuffer_reuses = s[Metric::framebuffer_reuses];
        const auto& dirty_full_fallbacks = s[Metric::dirty_full_fallbacks];
        const auto& dirty_full_fallback_predicted_bounds_missing = s[Metric::dirty_full_fallback_predicted_bounds_missing];
        const auto& dirty_full_fallback_composite_missing_input_bounds = s[Metric::dirty_full_fallback_composite_missing_input_bounds];
        const auto& dirty_full_fallback_transform_bounds_unknown = s[Metric::dirty_full_fallback_transform_bounds_unknown];
        const auto& dirty_full_fallback_effect_bounds_unknown = s[Metric::dirty_full_fallback_effect_bounds_unknown];

        out << "event=" << event << " count=" << total.size()
            << " total_p50_ms=" << format_ms(percentile(total, 0.50))
            << " total_p95_ms=" << format_ms(percentile(total, 0.95))
            << " setup_p95_ms=" << format_ms(percentile(setup, 0.95))
            << " composite_p95_ms=" << format_ms(percentile(composite, 0.95))
            << " blur_p95_ms=" << format_ms(percentile(blur, 0.95))
            << " encode_p95_ms=" << format_ms(percentile(encode, 0.95))
            << " ram_p95_mb=" << format_ms(percentile(ram, 0.95))
            << " cache_hits_p95=" << format_ms(percentile(cache_hits, 0.95))
            << " cache_misses_p95=" << format_ms(percentile(cache_misses, 0.95))
            << " nodes_executed_p95=" << format_ms(percentile(nodes_executed, 0.95))
            << " clear_skipped_calls_p95=" << format_ms(percentile(clear_skipped_calls, 0.95))
            << " clear_skipped_pixels_p95=" << format_ms(percentile(clear_skipped_pixels, 0.95))
            << " clear_calls_p95=" << format_ms(percentile(clear_calls, 0.95))
            << " clear_pixels_p95=" << format_ms(percentile(clear_pixels, 0.95))
            << " composite_calls_p95=" << format_ms(percentile(composite_calls, 0.95))
            << " composite_pixels_p95=" << format_ms(percentile(composite_pixels, 0.95))
            << " transform_calls_p95=" << format_ms(percentile(transform_calls, 0.95))
            << " transform_pixels_p95=" << format_ms(percentile(transform_pixels, 0.95))
            << " effect_stack_calls_p95=" << format_ms(percentile(effect_stack_calls, 0.95))
            << " effect_pixels_p95=" << format_ms(percentile(effect_pixels, 0.95))
            << " text_glyphs_rasterized_p95=" << format_ms(percentile(text_glyphs_rasterized, 0.95))
            << " framebuffer_allocations_p95=" << format_ms(percentile(framebuffer_allocations, 0.95))
            << " framebuffer_reuses_p95=" << format_ms(percentile(framebuffer_reuses, 0.95))
            << " dirty_full_fallbacks_total=" << format_count(max_value(dirty_full_fallbacks))
            << " dirty_full_fallback_predicted_bounds_missing_total=" << format_count(max_value(dirty_full_fallback_predicted_bounds_missing))
            << " dirty_full_fallback_composite_missing_input_bounds_total=" << format_count(max_value(dirty_full_fallback_composite_missing_input_bounds))
            << " dirty_full_fallback_transform_bounds_unknown_total=" << format_count(max_value(dirty_full_fallback_transform_bounds_unknown))
            << " dirty_full_fallback_effect_bounds_unknown_total=" << format_count(max_value(dirty_full_fallback_effect_bounds_unknown))
            << "\n";
    }

    const auto bottleneck_event = [&grouped]() {
        std::string best_event = "unknown";
        double best_ms = -1.0;
        for (const auto& [event, s] : grouped) {
            const double total_p95 = percentile(s[Metric::total], 0.95);
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
