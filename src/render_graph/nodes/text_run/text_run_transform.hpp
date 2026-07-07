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

/// Build the world-space model matrix for TextRun rendering.
///
/// The graph builder has already pre-computed the final world matrix
/// (including canvas-centre, 2.5D projection, and layer transform).
/// This helper only applies SSAA scaling on top.
///
/// Used by BOTH `TextRunNode::predicted_bbox()` and `execute()`.
glm::mat4 build_world_matrix(
    const RenderGraphContext& ctx,
    const TextRunPlacement& placement
);

}  // namespace chronon3d::graph::text_run
