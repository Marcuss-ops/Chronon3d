#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::telemetry {

struct RenderTelemetryRow;

struct NodeTelemetryRecord;
struct LayerTelemetryRecord;
struct CacheTelemetryRecord;
struct CullingTelemetryRecord;
struct TextTelemetryRecord;
struct ImageTelemetryRecord;
struct TileTelemetryRecord;

namespace detail {

// -----------------------------------------------------------------------
// Mutex getters
// -----------------------------------------------------------------------

std::mutex& telemetry_mutex();
std::mutex& node_telemetry_mutex();
std::mutex& layer_telemetry_mutex();
std::mutex& cache_telemetry_mutex();
std::mutex& culling_telemetry_mutex();
std::mutex& text_telemetry_mutex();
std::mutex& image_telemetry_mutex();
std::mutex& tile_telemetry_mutex();

// -----------------------------------------------------------------------
// Global / thread-local stores
// -----------------------------------------------------------------------

std::vector<RenderTelemetryRow>& global_rows();
std::vector<RenderTelemetryRow>& thread_local_buffer();

std::vector<NodeTelemetryRecord>& node_telemetry_store();
std::vector<LayerTelemetryRecord>& layer_telemetry_store();
std::vector<CacheTelemetryRecord>& cache_telemetry_store();
std::vector<CullingTelemetryRecord>& culling_telemetry_store();
std::vector<TextTelemetryRecord>& text_telemetry_store();
std::vector<ImageTelemetryRecord>& image_telemetry_store();
std::vector<TileTelemetryRecord>& tile_telemetry_store();

// -----------------------------------------------------------------------
// CSV path / I/O helpers
// -----------------------------------------------------------------------

std::filesystem::path telemetry_csv_path();
std::filesystem::path telemetry_summary_path();

void ensure_csv_header(std::ofstream& out, const std::filesystem::path& path);
bool csv_header_matches(const std::filesystem::path& path);
void migrate_legacy_csv(const std::filesystem::path& path);
std::size_t read_last_run_id(const std::filesystem::path& path);
std::size_t current_run_id();

// -----------------------------------------------------------------------
// String helpers (inline — pure computation, no mutable state)
// -----------------------------------------------------------------------

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

// -----------------------------------------------------------------------
// Summary file (declaration, implementation in .cpp)
// -----------------------------------------------------------------------

void write_summary_file(const std::vector<RenderTelemetryRow>& rows);

} // namespace detail
} // namespace chronon3d::telemetry
