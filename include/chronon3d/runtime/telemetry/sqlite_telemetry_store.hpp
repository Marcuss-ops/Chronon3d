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
    void begin_transaction() override;
    void end_transaction(bool commit) override;
    bool write_render_run(const RenderTelemetryRecord& run) override;
    bool write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) override;
    bool write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) override;
    bool write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) override;
    bool write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) override;
    bool write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) override;
    bool write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) override;
    bool write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) override;
    bool write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) override;
    bool write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) override;
    bool write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d::telemetry
