#pragma once
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <string>
#include <mutex>

namespace chronon3d::telemetry {

class JsonlTelemetryStore final : public TelemetryStore {
public:
    JsonlTelemetryStore() = default;
    ~JsonlTelemetryStore() override = default;

    bool initialize(const std::string& path) override;
    bool write_render_run(const RenderTelemetryRecord& run) override;
    bool write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) override;
    bool write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) override;
    bool write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) override;

private:
    std::string m_path;
    std::mutex m_mutex;
};

} // namespace chronon3d::telemetry
