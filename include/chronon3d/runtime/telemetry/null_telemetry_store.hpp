#pragma once
#include <chronon3d/runtime/telemetry/telemetry_store.hpp>
#include <string>
#include <vector>

namespace chronon3d::telemetry {

/// NullTelemetryStore — no-op TelemetryStore implementation.
///
/// Used when CHRONON3D_ENABLE_TELEMETRY=OFF.  All writes succeed silently;
/// the contract is preserved so callers don't need #ifdef guards at every
/// call site.
class NullTelemetryStore final : public TelemetryStore {
public:
    NullTelemetryStore() = default;
    ~NullTelemetryStore() override = default;

    bool initialize(const std::string& /*db_path*/) override { return true; }
    bool write_render_run(const RenderTelemetryRecord& /*run*/) override { return true; }
    bool write_frames(const std::string& /*run_id*/, const std::vector<FrameTelemetryRecord>& /*frames*/) override { return true; }
    bool write_phases(const std::string& /*run_id*/, const std::vector<PhaseTelemetryRecord>& /*phases*/) override { return true; }
    bool write_counters(const std::string& /*run_id*/, const std::vector<CounterTelemetryRecord>& /*counters*/) override { return true; }
    bool write_node_events(const std::string& /*run_id*/, const std::vector<NodeTelemetryRecord>& /*events*/) override { return true; }
    bool write_layer_events(const std::string& /*run_id*/, const std::vector<LayerTelemetryRecord>& /*events*/) override { return true; }
    bool write_cache_events(const std::string& /*run_id*/, const std::vector<CacheTelemetryRecord>& /*events*/) override { return true; }
    bool write_culling_events(const std::string& /*run_id*/, const std::vector<CullingTelemetryRecord>& /*events*/) override { return true; }
    bool write_text_events(const std::string& /*run_id*/, const std::vector<TextTelemetryRecord>& /*events*/) override { return true; }
    bool write_image_events(const std::string& /*run_id*/, const std::vector<ImageTelemetryRecord>& /*events*/) override { return true; }
    bool write_tile_events(const std::string& /*run_id*/, const std::vector<TileTelemetryRecord>& /*events*/) override { return true; }
};

} // namespace chronon3d::telemetry
