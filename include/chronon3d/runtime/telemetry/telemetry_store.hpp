#pragma once
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <string>
#include <vector>

namespace chronon3d::telemetry {

class TelemetryStore {
public:
    virtual ~TelemetryStore() = default;

    virtual bool initialize(const std::string& db_path) = 0;

    // Transaction boundary: begin_transaction()/end_transaction() bracket a batch of writes.
    // For SQL stores this wraps all writes in a single BEGIN/COMMIT.
    virtual void begin_transaction() {}
    virtual void end_transaction(bool commit) {}

    virtual bool write_render_run(const RenderTelemetryRecord& run) = 0;
    virtual bool write_frames(const std::string& run_id, const std::vector<FrameTelemetry>& frames) = 0;
    virtual bool write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) = 0;
    virtual bool write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) = 0;
    virtual bool write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) = 0;
    virtual bool write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) = 0;
    virtual bool write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) = 0;
    virtual bool write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) = 0;
    virtual bool write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) = 0;
    virtual bool write_artifacts(const std::string& run_id, const std::vector<RenderArtifactRecord>& artifacts) = 0;

    // Stage 3 memory persistence (end-of-run projections from
    // NodeMemoryTracker; never written from the hot path).
    virtual bool write_node_summaries(const std::string& run_id,
                                      const std::vector<NodeSummaryTelemetryRecord>& summaries) { return true; }
    virtual bool write_memory_summary(const std::string& run_id,
                                      const MemorySummaryTelemetryRecord& summary) { return true; }

    // Retention janitor for Detailed/Trace data ONLY (per-frame/per-event
    // tables). Durable Summary rows (render_runs, render_counters, phase
    // events, node/memory summaries, artifacts) are never touched: they are
    // the long-term proprietary corpus. Default no-op; the SQLite store
    // implements the purge. detail_ttl_days <= 0 disables the janitor.
    virtual void apply_retention(int detail_ttl_days) { (void)detail_ttl_days; }
};

} // namespace chronon3d::telemetry
