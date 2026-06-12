#pragma once

// ---------------------------------------------------------------------------
// frame_reuse_policy.hpp
//
// Encapsulates the three frame-reuse fast-path checks that allow
// render_scene_via_graph() to skip graph building + execution entirely
// when the previous frame's output can be returned directly.
//
// Three fast-paths, in priority order:
//   1. Resolved-scene reuse  — consecutive frame with identical fingerprint
//   2. Static-scene fast-path — unchanged scene + same camera + same frame
//   3. Empty dirty-rect reuse — dirty rect is empty (nothing changed)
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "scene_fingerprint.hpp"
#include "scene_internal.hpp"
#include <memory>

namespace chronon3d::graph {

/// Result of a frame-reuse evaluation.
/// When `can_reuse` is true, `framebuffer` contains the previous frame's
/// output and can be returned immediately.  `scene_structure_unchanged`
/// and related flags are still populated so callers can use them for
/// the graph-reuse hint regardless of whether the fast-path fired.
struct FrameReuseResult {
    bool can_reuse{false};
    std::shared_ptr<Framebuffer> framebuffer;

    // ── State for downstream consumers (populated even when !can_reuse) ──
    bool scene_structure_unchanged{false};
    bool scene_is_static{false};
    bool static_cam_changed{true};
    uint64_t current_static_fp{0};
    uint64_t current_active_at_fp{0};
    uint64_t current_structure_fp{0};
    uint64_t current_combined_fp{0};
};

/// Evaluate fast-path #1: resolved-scene reuse.
///
/// Conditions:
///   - Previous framebuffer exists with matching dimensions
///   - Frame is either the same as previous, or consecutive with static scene
///   - Camera is unchanged
///   - Static fingerprint + active-at fingerprint both match
///
/// This is the strictest fast-path: requires identical visual output.
[[nodiscard]] FrameReuseResult evaluate_resolved_scene_reuse(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    int width,
    int height,
    bool diagnostics_enabled);

/// Evaluate fast-path #2: static-scene fast-path.
///
/// Conditions:
///   - Previous framebuffer exists with matching dimensions
///   - Frame reuse allowed (same frame, or consecutive with static scene)
///   - Scene structure unchanged
///   - Camera unchanged
///   - Active-at fingerprint unchanged
///   - Static fingerprint matches
[[nodiscard]] FrameReuseResult evaluate_static_scene_fastpath(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    bool scene_structure_unchanged,
    bool scene_is_static,
    bool static_cam_changed,
    const FrameFingerprints& prev_fps,
    int width,
    int height,
    bool diagnostics_enabled);

/// Evaluate fast-path #3: empty dirty-rect reuse.
///
/// When dirty rect computation determined that nothing changed (empty rect),
/// return the previous framebuffer with updated state.
[[nodiscard]] FrameReuseResult evaluate_empty_dirty_reuse(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    const detail::DirtyRectOutput& dirty_out,
    const RenderSettings& settings,
    int width,
    int height,
    bool diagnostics_enabled);

} // namespace chronon3d::graph
