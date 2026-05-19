#pragma once
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <string>
#include <memory>

namespace chronon3d::telemetry {

class SqliteTelemetryStore final : public TelemetryStore {
public:
    SqliteTelemetryStore();
    ~SqliteTelemetryStore() override;

    bool initialize(const std::string& db_path) override;
    bool write_render_run(const RenderTelemetryRecord& run) override;
    bool write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) override;
    bool write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) override;
    bool write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d::telemetry
