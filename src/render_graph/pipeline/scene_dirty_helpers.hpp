#pragma once

// ---------------------------------------------------------------------------
// scene_dirty_helpers.hpp
//
// Declarations for internal helpers for dirty region tracking.
// Implementations moved to scene_dirty_helpers.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "scene_internal.hpp"
#include <optional>
#include <string>
#include <unordered_map>

namespace chronon3d::graph::detail {

// ── Scrolling shortcut ───────────────────────────────────────────────────────
// When only the camera position has changed by an integer amount we can shift
// the previous framebuffer and only re-render the newly exposed strip.
[[nodiscard]] std::optional<raster::BBox> try_scroll_optimization(
    SoftwareRenderer* sw_renderer,
    const Camera2_5D& cam25d,
    i32 width,
    i32 height);

// ── Parallel layer bbox computation ──────────────────────────────────────────
// Computes bboxes for all active resolved layers using TBB parallelism.
[[nodiscard]] std::unordered_map<std::string, LayerBBoxState> compute_layer_bboxes_parallel(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Camera2_5DRuntime& cam25d,
    SoftwareRenderer* sw_renderer,
    const RenderSettings& settings,
    i32 width,
    i32 height,
    Frame frame);

// ── Scene root node bboxes ───────────────────────────────────────────────────
// Computes bboxes for scene root nodes (non-layer sources).
void compute_scene_root_bboxes(
    std::unordered_map<std::string, LayerBBoxState>& bboxes,
    const Scene& scene,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer);

} // namespace chronon3d::graph::detail
