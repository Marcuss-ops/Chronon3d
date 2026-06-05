#pragma once

#include <chronon3d/core/telemetry/render_telemetry.hpp>

namespace chronon3d::telemetry {

/// Aggregated snapshot of all per-event telemetry stores.
struct TelemetryBundle {
    std::vector<NodeTelemetryRecord>   node_events;
    std::vector<LayerTelemetryRecord>  layer_events;
    std::vector<CacheTelemetryRecord>  cache_events;
    std::vector<CullingTelemetryRecord> culling_events;
    std::vector<TextTelemetryRecord>   text_events;
    std::vector<ImageTelemetryRecord>  image_events;
    std::vector<TileTelemetryRecord>   tile_events;
};

/// Drains all in-memory telemetry stores into a single bundle.
[[nodiscard]] inline TelemetryBundle collect_all_telemetry() {
    return {
        .node_events   = collect_node_telemetry(),
        .layer_events  = collect_layer_telemetry(),
        .cache_events  = collect_cache_telemetry(),
        .culling_events = collect_culling_telemetry(),
        .text_events   = collect_text_telemetry(),
        .image_events  = collect_image_telemetry(),
        .tile_events   = collect_tile_telemetry(),
    };
}

} // namespace chronon3d::telemetry
