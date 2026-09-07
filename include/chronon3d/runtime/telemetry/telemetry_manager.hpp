#pragma once
#include <chronon3d/runtime/telemetry/telemetry_level.hpp>
#include <chronon3d/runtime/telemetry/telemetry_run_snapshot.hpp>
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <vector>
#include <memory>
#include <string>

namespace chronon3d::telemetry {

/// Immutable process-boundary observability configuration.
/// Environment variables are resolved by Config/CLI startup; telemetry runtime
/// receives values and must not call getenv() on record paths.
struct TelemetryRuntimeConfig {
    std::string run_id_override;

    /// Capture level (boundary-resolved; default = Summary). Granular
    /// per-frame/per-event capture happens only at Detailed/Trace.
    TelemetryLevel level{kDefaultTelemetryLevel};
};

class TelemetryManager {
public:
    static TelemetryManager& instance();

    TelemetryManager();
    ~TelemetryManager() = default;

    void configure(TelemetryRuntimeConfig config);

    // Capture-level override (equivalent to configure() with only `level`
    // set). Convenience for hosts/tests that keep the default boundary paths.
    void set_level(TelemetryLevel level) noexcept;

    // Registers a custom backend store.
    void add_store(std::shared_ptr<TelemetryStore> store);
    void clear_stores();

    // Sets up the default events-only store (NullTelemetryStore) from
    // pre-resolved boundary config. Wave 1D: no database store is bundled.
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

private:
    TelemetryRuntimeConfig m_config{};
    std::vector<std::shared_ptr<TelemetryStore>> m_stores;
};

} // namespace chronon3d::telemetry
