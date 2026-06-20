#pragma once

// ---------------------------------------------------------------------------
// runtime/frame_history.hpp
//
// Per-frame camera + fingerprint history — canonical definition.
//
// Migrated out of `math/renderer_state.hpp` as part of the RenderSession
// extraction (see "Spostare e separare RenderSession", section 8.4).
// Pre-Phase-2 this header was a `using` alias on top of
// `RendererFrameHistory`; from Phase 2 onward the canonical struct lives
// here, and `math/renderer_state.hpp` exposes a backward-compatibility
// alias `RendererFrameHistory = ::chronon3d::FrameHistory`.
//
// Field names (`prev_frame`, `prev_camera`, `prev_camera_valid`,
// `prev_scene_fingerprint`, `prev_static_scene_fingerprint`,
// `prev_graph_structure_fingerprint`, `prev_active_at_fingerprint`) are
// preserved verbatim — keeping the existing ~120 call-sites untouched.
// ---------------------------------------------------------------------------

#include <cstdint>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d {

/// Per-frame camera + fingerprint history.
///
/// Carried forward from the previous frame's render_scene_via_graph() call
/// for use by fast-path reuse checks and dirty-rect diffing.
struct FrameHistory {
    Frame prev_frame{-1};
    Camera2_5D prev_camera;
    bool prev_camera_valid{false};
    uint64_t prev_scene_fingerprint{0};
    uint64_t prev_static_scene_fingerprint{0};
    uint64_t prev_graph_structure_fingerprint{0};
    uint64_t prev_active_at_fingerprint{0};
};

} // namespace chronon3d
