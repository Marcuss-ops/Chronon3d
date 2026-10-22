#pragma once

#include <chronon3d/core/telemetry/render_telemetry.hpp>

namespace chronon3d::telemetry {

/// Aggregated snapshot of all per-event telemetry stores.
struct TelemetryBundle {
    std::vector<NodeTelemetryRecord>   node_events;
    std::vector<LayerTelemetryRecord>  layer_events;
    std::vector<CacheTelemetryRecord>  cache_events;
    std::vector<CullingTelemetryRecord> culling_events;
    std::vector<ImageTelemetryRecord>  image_events;
};


} // namespace chronon3d::telemetry
