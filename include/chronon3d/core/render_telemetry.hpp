#pragma once

#include <chronon3d/core/types.hpp>

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

namespace chronon3d::telemetry {

struct RenderTelemetryRow {
    std::string event;
    Frame frame{0};
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
};

namespace detail {

inline std::mutex& telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::vector<RenderTelemetryRow>& recent_rows() {
    static std::vector<RenderTelemetryRow> rows;
    return rows;
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
        out << "ts,run_id,event,frame,w,h,total_ms,setup_ms,composite_ms,blur_ms,encode_ms,ram_mb,cache_hit,layer_count\n";
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
    return header == "ts,run_id,event,frame,w,h,total_ms,setup_ms,composite_ms,blur_ms,encode_ms,ram_mb,cache_hit,layer_count";
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
    out << "recent_rows=" << rows.size() << "\n";

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
    }

    for (const auto& [event, s] : grouped) {
        out << "event=" << event << " count=" << s.total.size()
            << " total_p50_ms=" << format_ms(percentile(s.total, 0.50))
            << " total_p95_ms=" << format_ms(percentile(s.total, 0.95))
            << " setup_p95_ms=" << format_ms(percentile(s.setup, 0.95))
            << " composite_p95_ms=" << format_ms(percentile(s.composite, 0.95))
            << " blur_p95_ms=" << format_ms(percentile(s.blur, 0.95))
            << " encode_p95_ms=" << format_ms(percentile(s.encode, 0.95))
            << " ram_p95_mb=" << format_ms(percentile(s.ram, 0.95))
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

    const auto slowest_row = std::max_element(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.total_ms < b.total_ms;
    });
    if (slowest_row != rows.end()) {
        out << "slowest_event=" << slowest_row->event
            << " frame=" << slowest_row->frame
            << " total_ms=" << format_ms(slowest_row->total_ms)
            << " setup_ms=" << format_ms(slowest_row->setup_ms)
            << " composite_ms=" << format_ms(slowest_row->composite_ms)
            << " blur_ms=" << format_ms(slowest_row->blur_ms)
            << " encode_ms=" << format_ms(slowest_row->encode_ms)
            << " cache_hit=" << slowest_row->cache_hit
            << " layer_count=" << slowest_row->layer_count
            << "\n";
    }
}

} // namespace detail

inline void record_render_telemetry(const RenderTelemetryRow& row) {
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

    auto& rows = detail::recent_rows();
    rows.push_back(row);
    if (rows.size() > 100) {
        rows.erase(rows.begin());
    }

    std::ofstream csv(csv_path, std::ios::app);
    if (csv) {
        detail::ensure_csv_header(csv, csv_path);
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
            << row.layer_count << '\n';
    }

    detail::write_summary_file(rows);
}

} // namespace chronon3d::telemetry
