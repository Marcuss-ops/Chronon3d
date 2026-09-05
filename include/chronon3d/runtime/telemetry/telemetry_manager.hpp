#pragma once
#include <chronon3d/runtime/telemetry/telemetry_level.hpp>
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

    /// Capture level (boundary-resolved; default = Summary). Granular
    /// per-frame/per-event tables are only persisted at Detailed/Trace;
    /// durable Summary rows are always persisted (never auto-rotated).
    TelemetryLevel level{kDefaultTelemetryLevel};
    /// Retention window (days) for Detailed/Trace data only. 0 disables the
    /// janitor. Summary data (runs/counters/summaries) is never purged.
    int detail_ttl_days{30};
};

class TelemetryManager {
public:
    static TelemetryManager& instance();

    TelemetryManager();
    ~TelemetryManager() = default;

    void configure(TelemetryRuntimeConfig config);

    // Capture-level override (equivalent to configure() with only `level`
    // set). Convenience for hosts/tests that keep the default boundary paths.
    void set_level(TelemetryLevel level) noexcept { m_config.level = level; }

    // Registers a custom backend store.
    void add_store(std::shared_ptr<TelemetryStore> store);
    void clear_stores();

    // Sets up the default SQLite/null store from pre-resolved boundary config.
    void initialize_default_stores();

    // Canonical entry point: one completed run, one immutable snapshot,
    // one transaction per store ("whole run or nothing").
    bool record_run(const TelemetryRunSnapshot& snapshot);

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
