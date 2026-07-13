// ---------------------------------------------------------------------------
// scene_reuse_coordinator.hpp
//
// Internal-only header for the Phase 1+2+3 coordinator helper extracted
// from scene.cpp (Azione 19).  This header is NOT exposed to the SDK
// surface (does not live under include/chronon3d/) and is consumed
// exclusively by scene.cpp (caller) and scene_reuse_coordinator.cpp
// (impl).
//
// Coordinates THREE pre-dirty-rect evaluations:
//   1. Resolved-scene reuse fast-path       (framebuffer returnable)
//   2. Fingerprint computation + derived state (used by Phase 4 hint + Phase 7)
//   3. Static-scene fast-path               (framebuffer returnable)
//
// When ONE of the fast-paths fires, the caller (scene.cpp) returns early.
// Otherwise the caller continues to Phase 4 (graph_structure_unchanged hint)
// and Phase 5+ (full dirty + graph build + execute path).
// ---------------------------------------------------------------------------

#pragma once

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "scene_fingerprint.hpp"   // src-only header (Check 17 architecture-boundary)
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <memory>

namespace chronon3d::graph::detail {

/// Aggregated result of evaluate_early_reuse_phases().
///
/// `fast_path_reuse_fb` is non-null when fast-path #1 (resolved-scene reuse)
/// or fast-path #2 (static-scene fast-path) hits; callers return immediately.
/// `fast_path_reuse_fb` is null when the caller must continue to Phase 4+;
/// in that case the struct's fingerprint-derived state is fully populated.
///
/// The post-evaluate empty-dirty-rect path (Phase 7) is NOT included here
/// because it depends on Phase 5's dirty_out; that path stays in scene.cpp.
struct ReuseEvaluation {
    /// Early-out framebuffer (Phase 1 or Phase 3 fast-path).  Null means
    /// the caller must continue the full pipeline.  When non-null, callers
    /// can return it directly without further work.
    std::shared_ptr<Framebuffer> fast_path_reuse_fb;

    /// Frame-level fingerprint state (populated whenever sw_renderer is set).
    FrameFingerprints frame_fp{};
    FrameFingerprints prev_fp{};

    /// True when graph topology matches the previous frame (structure_fp).
    bool scene_structure_unchanged{false};

    /// True when the camera moved since the previous frame.
    bool static_cam_changed{true};

    /// True when the scene has no animated content at this frame.
    bool scene_is_static{false};
};

// Evaluates Phase 1 (resolved-scene reuse) + Phase 2 (fingerprints +
// derived state) + Phase 3 (static-scene fast-path).  Returns a struct
// whose `fast_path_reuse_fb` is set when one of the fast-paths hits.
//
// `ctx.frame_input.camera_2_5d` is read for the fast-path's camera check.
// `sw_renderer` may be nullptr (test paths, non-software backends); in that
// case all derived-state defaults are returned (no fast-path fires).
[[nodiscard]] ReuseEvaluation evaluate_early_reuse_phases(
    const RenderGraphContext& ctx,
    const Scene& scene,
    Frame frame,
    int width,
    int height,
    SoftwareRenderer* sw_renderer);

} // namespace chronon3d::graph::detail
