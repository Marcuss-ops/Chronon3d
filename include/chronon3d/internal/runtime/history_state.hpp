#pragma once

// ---------------------------------------------------------------------------
// runtime/history_state.hpp
//
// Internal, non-owning boundary for RenderSession's generic temporal state.
// The session remains the sole owner of FrameHistory and DirtyHistory. Facades
// are ephemeral and must not outlive the referenced RenderSession. Software
// framebuffer-ring and transform-scratch history remains owned by
// SoftwareSessionResources and is reset by SoftwareRenderSession's existing
// reset_temporal_history() orchestration.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/frame_history.hpp>

namespace chronon3d::runtime {

class FrameHistoryState final {
public:
    FrameHistoryState(FrameHistory& frame_history, DirtyHistory& dirty_history) noexcept
        : m_frame_history(&frame_history), m_dirty_history(&dirty_history) {}

    /// Reset generic temporal history and previous-layer dirty state only.
    /// Software-specific ring/scratch resources are reset by the software
    /// session wrapper, not by this renderer-agnostic facade.
    void reset() {
        *m_frame_history = FrameHistory{};
        m_dirty_history->previous_layers.clear();
    }

    /// Reset frame telemetry while preserving previous-layer history.
    void reset_frame_temporaries() {
        m_dirty_history->reset_telemetry_counters();
    }

    [[nodiscard]] FrameHistory& frame_history() noexcept { return *m_frame_history; }
    [[nodiscard]] const FrameHistory& frame_history() const noexcept { return *m_frame_history; }
    [[nodiscard]] DirtyHistory& dirty_history() noexcept { return *m_dirty_history; }
    [[nodiscard]] const DirtyHistory& dirty_history() const noexcept { return *m_dirty_history; }

private:
    FrameHistory* m_frame_history;
    DirtyHistory* m_dirty_history;
};

} // namespace chronon3d::runtime
