#pragma once
#include <chronon3d/runtime/telemetry/telemetry_run_snapshot.hpp>
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <vector>
#include <memory>
#include <string>
#include <filesystem>

namespace chronon3d::telemetry {

/// Immutable process-boundary observability configuration.
/// Environment variables are resolved by Config/CLI startup; telemetry runtime
/// receives values and must not call getenv() on record or store paths.
struct TelemetryRuntimeConfig {
    std::filesystem::path path_override;
    std::filesystem::path default_directory{"/tmp/.chronon3d/telemetry"};
    std::string run_id_override;
};

class TelemetryManager {
public:
    static TelemetryManager& instance();

    TelemetryManager();
    ~TelemetryManager() = default;

    void configure(TelemetryRuntimeConfig config);

    // Registers a custom backend store.
    void add_store(std::shared_ptr<TelemetryStore> store);
    void clear_stores();

    // Sets up the default SQLite/null store from pre-resolved boundary config.
    void initialize_default_stores();

    // Canonical entry point: one completed run, one immutable snapshot,
    // one transaction per store ("whole run or nothing").
    bool record_run(const TelemetryRunSnapshot& snapshot);

    // Compatibility overload — builds a snapshot and forwards.
    // DEMOLITION DEBT (TICKET-TELEMETRY-SQLITE-NORMALIZATION Stage 2):
    // remove once all callers construct TelemetryRunSnapshot directly.
    bool record_run(RenderTelemetryRecord& run,
                    const std::vector<FrameTelemetry>& frames = {},
                    const std::vector<PhaseTelemetryRecord>& phases = {},
                    const std::vector<CounterTelemetryRecord>& counters = {},
                    const std::vector<NodeTelemetryRecord>& node_events = {},
                    const std::vector<LayerTelemetryRecord>& layer_events = {},
                    const std::vector<CacheTelemetryRecord>& cache_events = {},
                    const std::vector<CullingTelemetryRecord>& culling_events = {},
                    const std::vector<ImageTelemetryRecord>& image_events = {},
                    const std::vector<RenderArtifactRecord>& artifacts = {});

    static std::string get_os_name();
    static std::string get_cpu_model();
    static int get_logical_cores();
    static std::string get_compiler_info();
    static std::string get_build_type();
    static std::string get_git_commit();
    static std::string get_current_iso_time();
    static std::string generate_uuid();
    static uint64_t get_peak_memory_usage();

    /// Compatibility helper: resolves against the singleton's already
    /// configured boundary value. It performs no environment lookup.
    static std::filesystem::path resolve_sqlite_telemetry_path();
    static std::filesystem::path resolve_sqlite_telemetry_path(
        const TelemetryRuntimeConfig& config);

private:
    TelemetryRuntimeConfig m_config{};
    std::vector<std::shared_ptr<TelemetryStore>> m_stores;
};

} // namespace chronon3d::telemetry
