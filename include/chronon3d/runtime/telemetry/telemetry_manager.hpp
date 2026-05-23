#pragma once
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <vector>
#include <memory>
#include <string>
#include <filesystem>

namespace chronon3d::telemetry {

class TelemetryManager {
public:
    static TelemetryManager& instance();

    TelemetryManager();
    ~TelemetryManager() = default;

    // Registers a custom backend store
    void add_store(std::shared_ptr<TelemetryStore> store);

    // Clears all registered stores
    void clear_stores();

    // Sets up default stores (JSONL and SQLite if enabled) in default user directory
    void initialize_default_stores();

    // Gathers system info and writes record across all stores
    bool record_run(RenderTelemetryRecord& run,
                    const std::vector<FrameTelemetryRecord>& frames = {},
                    const std::vector<PhaseTelemetryRecord>& phases = {},
                    const std::vector<CounterTelemetryRecord>& counters = {},
                    const std::vector<NodeTelemetryRecord>& node_events = {},
                    const std::vector<LayerTelemetryRecord>& layer_events = {},
                    const std::vector<CacheTelemetryRecord>& cache_events = {},
                    const std::vector<CullingTelemetryRecord>& culling_events = {},
                    const std::vector<TextTelemetryRecord>& text_events = {},
                    const std::vector<ImageTelemetryRecord>& image_events = {},
                    const std::vector<TileTelemetryRecord>& tile_events = {});

    // System helper queries
    static std::string get_os_name();
    static std::string get_cpu_model();
    static int get_logical_cores();
    static std::string get_compiler_info();
    static std::string get_build_type();
    static std::string get_git_commit();
    static std::string get_current_iso_time();
    static std::string generate_uuid();
    static uint64_t get_peak_memory_usage();
    static std::filesystem::path resolve_sqlite_telemetry_path();

private:
    std::vector<std::shared_ptr<TelemetryStore>> m_stores;
};

} // namespace chronon3d::telemetry
