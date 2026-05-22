#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>

#ifndef CHRONON3D_ENABLE_SQLITE_TELEMETRY

namespace chronon3d::telemetry {

struct SqliteTelemetryStore::Impl {};

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}
SqliteTelemetryStore::~SqliteTelemetryStore() = default;

bool SqliteTelemetryStore::initialize(const std::string&) { return false; }
bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord&) { return false; }
bool SqliteTelemetryStore::write_frames(const std::string&, const std::vector<FrameTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_phases(const std::string&, const std::vector<PhaseTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_counters(const std::string&, const std::vector<CounterTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_node_events(const std::string&, const std::vector<NodeTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_layer_events(const std::string&, const std::vector<LayerTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_cache_events(const std::string&, const std::vector<CacheTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_culling_events(const std::string&, const std::vector<CullingTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_text_events(const std::string&, const std::vector<TextTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_tile_events(const std::string&, const std::vector<TileTelemetryRecord>&) { return false; }

} // namespace chronon3d::telemetry

#endif
