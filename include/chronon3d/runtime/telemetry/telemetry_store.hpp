#pragma once
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <string>
#include <vector>

namespace chronon3d::telemetry {

class TelemetryStore {
public:
    virtual ~TelemetryStore() = default;

    virtual bool initialize(const std::string& db_path) = 0;
    virtual bool write_render_run(const RenderTelemetryRecord& run) = 0;
    virtual bool write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) = 0;
    virtual bool write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) = 0;
    virtual bool write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) = 0;
    virtual bool write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) = 0;
    virtual bool write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) = 0;
    virtual bool write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) = 0;
    virtual bool write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) = 0;
    virtual bool write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) = 0;
    virtual bool write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) = 0;
    virtual bool write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) = 0;
};

} // namespace chronon3d::telemetry
