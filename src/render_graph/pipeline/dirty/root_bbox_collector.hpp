#pragma once

// ── Scene root-node bounding-box collector ─────────────────────────────────
//
/// @file    root_bbox_collector.hpp
/// @brief   Computes bounding boxes for scene root nodes (non-layer sources
///          such as shapes, backgrounds) and adds them to the per-layer
///          bbox map for dirty-rect tracking.
///
/// Extracted from scene_dirty_helpers.hpp during PR 4 split.

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
#include <string>
#include <unordered_map>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph::detail {

/// Compute bboxes for all visible scene root nodes and insert them into
/// the bboxes map (keyed as "root.node:<name>").
void compute_scene_root_bboxes(
    std::unordered_map<std::string, LayerBBoxState>& bboxes,
    const Scene& scene,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer);

} // namespace chronon3d::graph::detail
