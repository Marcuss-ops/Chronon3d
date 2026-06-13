#pragma once

// ---------------------------------------------------------------------------
// renderer_frame_state.hpp
//
/// @file    renderer_frame_state.hpp
/// @brief   Aggregated per-frame state for SoftwareRenderer.
///
/// Groups the buffer ring, scratch buffer, frame history, dirty telemetry,
/// layer history, and scene hasher into a single struct so that
/// SoftwareRenderer's member list is more compact and the per-frame
/// state is easier to manage as a unit.
//
// SoftwareRenderer will have a single member:
//   RendererFrameState m_frame_state;
//
// Accessors (buffer_ring(), frame_history(), etc.) remain unchanged —
// the grouping is purely an internal implementation detail.
// ===========================================================================

#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>

namespace chronon3d {

/// Per-frame state for SoftwareRenderer.
///
/// All members are default-constructible and movable, matching the
/// semantics of SoftwareRenderer (which is movable but not copyable).
struct RendererFrameState {
    RendererBufferRing       buffer_ring;
    TransformScratchBuffer   scratch_buffer;
    RendererFrameHistory     frame_history;
    RendererDirtyTelemetry   dirty_telemetry;
    RendererLayerHistory     layer_history;
    graph::SceneHasher       scene_hasher;
};

} // namespace chronon3d
