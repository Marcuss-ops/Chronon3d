#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// telemetry_session.hpp — the single, per-render-job telemetry collector.
//
// Chronon has ONE telemetry subsystem.  Everything that happens during a
// render — job-level timings, per-frame records, counters, node/layer/…/tile
// events — is recorded into a TelemetrySession and nothing else.  JSON,
// console and SQLite are *sinks*: they read a session/report, they never
// write metrics back into it.
//
// The session is deliberately NOT a global, NOT a singleton and NOT
// process-wide.  A RenderJob owns exactly one session, so a future daemon
// can run multiple jobs concurrently, each with its own isolated collector.
//
// This first step reuses the EXISTING record types (RenderTelemetryRecord,
// FrameTelemetry, Node/Layer/Cache/Culling/Text/Image/TileTelemetryRecord)
// and the EXISTING RenderCounters vocabulary — it does not introduce a
// parallel world of new types or a second registry/collector.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/profiling/counters.hpp>   // RenderCounters, RenderCountersRaw + X-macros
#include <chronon3d/core/profiling/profiling.hpp>  // profiling::Clock (wall timing source)
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::telemetry {

class TelemetrySession {
public:
    TelemetrySession() = default;
    ~TelemetrySession() = default;

    // Non-copyable.  Movable so a job can own the session by value and hand
    // it to the sink at finalize time.
    TelemetrySession(const TelemetrySession&) = delete;
    TelemetrySession& operator=(const TelemetrySession&) = delete;
    TelemetrySession(TelemetrySession&&) noexcept = default;
    TelemetrySession& operator=(TelemetrySession&&) noexcept = default;

    // ── Job lifecycle ──────────────────────────────────────────────────
    //
    // Opens the session for one render job and preallocates the frame slots
    // up front.  Preallocating lets the render thread and the encoder thread
    // write disjoint fields of the SAME slot (frames[N]) without reallocation
    // and without a final merge pass keyed on frame number.
    void begin_job(std::string composition_id,
                   std::string output_path,
                   int total_frames);

    // Preallocate (or grow) the per-frame slot vector to `total_frames`.
    // Call before the render loop starts so writers never trigger a realloc.
    void preallocate_frames(int total_frames);

    // ── Frame lifecycle ────────────────────────────────────────────────
    //
    // Canonical per-frame record.  Writers must only touch the fields they
    // own: the render thread writes render/conversion/cache fields, the
    // encoder thread writes encoder fields.  No lock is required because the
    // two writers never overlap on the same field.
    FrameTelemetry& frame(int frame_number);
    const FrameTelemetry& frame(int frame_number) const;

    // ── Counter snapshot ───────────────────────────────────────────────
    //
    // Single direction: RenderCounters → session.  The session collects raw
    // frame-by-frame data; derived metrics (mean, percentiles, fps, rates)
    // are computed only at finalize time, never incrementally during render.
    //
    // Takes a plain snapshot of the atomic RenderCounters into the
    // non-atomic RenderCountersRaw owned by this session, so the sink sees a
    // stable value regardless of any concurrent writer activity.
    void snapshot_counters(const RenderCounters& counters);
    const RenderCountersRaw& counters() const noexcept;

    // ── Detail events (reuse existing record types) ────────────────────
    void record_node(NodeTelemetryRecord rec);
    void record_layer(LayerTelemetryRecord rec);
    void record_cache(CacheTelemetryRecord rec);
    void record_culling(CullingTelemetryRecord rec);
    void record_text(TextTelemetryRecord rec);
    void record_image(ImageTelemetryRecord rec);
    void record_tile(TileTelemetryRecord rec);

    // ── Accessors (sink-facing) ────────────────────────────────────────
    RenderTelemetryRecord& run() noexcept;
    const RenderTelemetryRecord& run() const noexcept;

    const std::vector<FrameTelemetry>& frames() const noexcept;
    const RenderCountersRaw& counters_snapshot() const noexcept;

    const std::vector<NodeTelemetryRecord>& node_events() const noexcept;
    const std::vector<LayerTelemetryRecord>& layer_events() const noexcept;
    const std::vector<CacheTelemetryRecord>& cache_events() const noexcept;
    const std::vector<CullingTelemetryRecord>& culling_events() const noexcept;
    const std::vector<TextTelemetryRecord>& text_events() const noexcept;
    const std::vector<ImageTelemetryRecord>& image_events() const noexcept;
    const std::vector<TileTelemetryRecord>& tile_events() const noexcept;

private:
    RenderTelemetryRecord m_run;
    std::vector<FrameTelemetry> m_frames;
    RenderCountersRaw m_counters;

    std::vector<NodeTelemetryRecord> m_node_events;
    std::vector<LayerTelemetryRecord> m_layer_events;
    std::vector<CacheTelemetryRecord> m_cache_events;
    std::vector<CullingTelemetryRecord> m_culling_events;
    std::vector<TextTelemetryRecord> m_text_events;
    std::vector<ImageTelemetryRecord> m_image_events;
    std::vector<TileTelemetryRecord> m_tile_events;
};

// ── Inline implementation ─────────────────────────────────────────────────

inline void TelemetrySession::begin_job(std::string composition_id,
                                        std::string output_path,
                                        int total_frames) {
    m_run = RenderTelemetryRecord{};
    m_run.composition_id = std::move(composition_id);
    m_run.output_path = std::move(output_path);
    m_run.frames_total = total_frames;
    preallocate_frames(total_frames);
}

inline void TelemetrySession::preallocate_frames(int total_frames) {
    if (total_frames <= 0) {
        return;
    }
    const auto target = static_cast<std::size_t>(total_frames);
    if (m_frames.size() < target) {
        m_frames.resize(target);
    }
    for (int i = 0; i < total_frames; ++i) {
        m_frames[static_cast<std::size_t>(i)].frame_number = i;
    }
}

inline FrameTelemetry& TelemetrySession::frame(int frame_number) {
    const auto index = static_cast<std::size_t>(frame_number);
    if (index >= m_frames.size()) {
        m_frames.resize(index + 1);
    }
    m_frames[index].frame_number = frame_number;
    return m_frames[index];
}

inline const FrameTelemetry& TelemetrySession::frame(int frame_number) const {
    return m_frames.at(static_cast<std::size_t>(frame_number));
}

inline void TelemetrySession::snapshot_counters(const RenderCounters& counters) {
#define X(name) m_counters.name = counters.name.load(std::memory_order_relaxed);
    CHRONON_RENDER_COUNTERS(X)
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
    for (std::size_t i = 0; i < m_counters.dirty_full_fallback_reasons.size(); ++i) {
        m_counters.dirty_full_fallback_reasons[i] =
            counters.dirty_full_fallback_reasons[i].value.load(std::memory_order_relaxed);
    }
}

inline const RenderCountersRaw& TelemetrySession::counters() const noexcept {
    return m_counters;
}

inline void TelemetrySession::record_node(NodeTelemetryRecord rec) {
    m_node_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_layer(LayerTelemetryRecord rec) {
    m_layer_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_cache(CacheTelemetryRecord rec) {
    m_cache_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_culling(CullingTelemetryRecord rec) {
    m_culling_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_text(TextTelemetryRecord rec) {
    m_text_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_image(ImageTelemetryRecord rec) {
    m_image_events.push_back(std::move(rec));
}

inline void TelemetrySession::record_tile(TileTelemetryRecord rec) {
    m_tile_events.push_back(std::move(rec));
}

inline RenderTelemetryRecord& TelemetrySession::run() noexcept {
    return m_run;
}

inline const RenderTelemetryRecord& TelemetrySession::run() const noexcept {
    return m_run;
}

inline const std::vector<FrameTelemetry>& TelemetrySession::frames() const noexcept {
    return m_frames;
}

inline const RenderCountersRaw& TelemetrySession::counters_snapshot() const noexcept {
    return m_counters;
}

inline const std::vector<NodeTelemetryRecord>& TelemetrySession::node_events() const noexcept {
    return m_node_events;
}

inline const std::vector<LayerTelemetryRecord>& TelemetrySession::layer_events() const noexcept {
    return m_layer_events;
}

inline const std::vector<CacheTelemetryRecord>& TelemetrySession::cache_events() const noexcept {
    return m_cache_events;
}

inline const std::vector<CullingTelemetryRecord>& TelemetrySession::culling_events() const noexcept {
    return m_culling_events;
}

inline const std::vector<TextTelemetryRecord>& TelemetrySession::text_events() const noexcept {
    return m_text_events;
}

inline const std::vector<ImageTelemetryRecord>& TelemetrySession::image_events() const noexcept {
    return m_image_events;
}

inline const std::vector<TileTelemetryRecord>& TelemetrySession::tile_events() const noexcept {
    return m_tile_events;
}

} // namespace chronon3d::telemetry
