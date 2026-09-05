#pragma once

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <string>
#include <vector>

namespace chronon3d::telemetry {

/// Immutable photograph of one completed render run.
///
/// This is a VALUE OBJECT, not an authority: it measures nothing, holds no
/// state across runs, has no mutex and no singleton. Measurement authorities
/// (RenderCounters, NodeMemoryTracker, the per-event ShardedTelemetryStores)
/// are drained once at the end-of-run barrier into this struct; the only
/// permitted direction afterwards is
///
///     snapshot → TelemetryManager → TelemetryStore → SQLite
///
/// (TICKET-TELEMETRY-SQLITE-NORMALIZATION, Stage 2.)
struct TelemetryRunSnapshot {
    RenderTelemetryRecord run;

    std::vector<FrameTelemetry> frames;
    std::vector<PhaseTelemetryRecord> phases;
    std::vector<CounterTelemetryRecord> counters;

    std::vector<NodeTelemetryRecord> node_events;
    std::vector<LayerTelemetryRecord> layer_events;
    std::vector<CacheTelemetryRecord> cache_events;
    std::vector<CullingTelemetryRecord> culling_events;
    std::vector<ImageTelemetryRecord> image_events;

    std::vector<NodeSummaryTelemetryRecord> node_summaries;
    MemorySummaryTelemetryRecord memory_summary{};

    std::vector<RenderArtifactRecord> artifacts;
};

} // namespace chronon3d::telemetry
