// SPDX-License-Identifier: MIT
//
// M1.5#1 — internal transform helper for TextRunNode.
// The previous TextRunNode.cpp duplicated the world-matrix build
// between `predicted_bbox()` and `execute()` (SSAA + canvas-center
// composition, with/without 2.5D branch).  This single helperpoint
// removes the duplication and keeps both sites identical by
// construction.
//
// Internal — NOT in include/chronon3d/.

#pragma once

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#include <optional>

#include <glm/glm.hpp>

namespace chronon3d::graph::text_run {

/// Single authority for the TextRun world-space model matrix.
///
/// Consumed by BOTH `TextRunNode::predicted_bbox()` and the rasterization
/// dispatch (`render_text_run_item` → draw), plus diagnostics/overlay/audit
/// paths, so the bbox sampling and the real pixels share one matrix by
/// construction.
///
/// Resolution stages:
///   1. Tight surfaces (projected/local rasters): return the surface-local
///      basis `T(-surface_origin)`. The producer framebuffer is a local
///      [0,size) raster whose glyph composer already applies the run-local
///      offset, so canvas/projected decisions must NOT leak here. Matches
///      the {0,surface_size} box predicted_bbox() reports for tight runs.
///   2. Canvas paths: `ssaa_scale * placement.matrix` where placement.matrix
///      was pre-resolved ONCE by `resolve_text_run_placement()`
///      (layer placement × node-local placement, canvas-centre + 2.5D when
///      applicable). No centering or projection decisions happen here.
glm::mat4 build_world_matrix(
    const RenderGraphContext& ctx,
    const TextRunPlacement& placement
);

}  // namespace chronon3d::graph::text_run
