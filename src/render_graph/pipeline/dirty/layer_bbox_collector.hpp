#pragma once

// ── Parallel layer bounding-box collector ──────────────────────────────────
//
/// @file    layer_bbox_collector.hpp
/// @brief   Computes bounding boxes for all active resolved layers using TBB
///          parallelism.  Each layer's bbox is projected through the camera
///          (if applicable) and compared against the previous frame for
///          dirty-rect tracking.
///
/// Extracted from scene_dirty_helpers.hpp during PR 4 split.

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <string>
#include <unordered_map>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph::detail {

struct LayerResolutionResult;

/// Compute per-layer bboxes for all active resolved layers in parallel.
/// Returns a map from layer name to LayerBBoxState for the current frame.
[[nodiscard]] std::unordered_map<std::string, LayerBBoxState> compute_layer_bboxes_parallel(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Camera2_5DRuntime& cam25d,
    SoftwareRenderer* sw_renderer,
    const RenderSettings& settings,
    i32 width,
    i32 height,
    Frame frame);

} // namespace chronon3d::graph::detail
