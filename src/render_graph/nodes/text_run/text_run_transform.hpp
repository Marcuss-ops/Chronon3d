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
#include <chronon3d/scene/model/render/render_node.hpp>

#include <optional>

#include <glm/glm.hpp>

namespace chronon3d::graph::text_run {

/// Build the world-space model matrix consumed by BOTH
/// `TextRunNode::predicted_bbox()` (sampling for downstream
/// composition) AND `backend->draw_text_run()` (rasterization).
///
/// Mirrors SourceNode's transform composition rules so the canvas
/// coordinate space is identical across nodes:
///
///   - SSAA scale (x, y; z stays 1.0)
///   - canvas-center translate (when centered || 2.5D)
///   - matrix_override  OR  render_ref.world_transform.to_mat4()
///
/// `uses_2_5d_projection` and `centered` both trigger the
/// canvas-center branch; the matrix override takes priority over
/// the RenderNode's world_transform when present (modular-coordinates
/// plumbing).
glm::mat4 build_world_matrix(
    const ::chronon3d::RenderNode& render_ref,
    const RenderGraphContext& ctx,
    bool uses_2_5d_projection,
    bool centered,
    std::optional<glm::mat4> matrix_override
);

}  // namespace chronon3d::graph::text_run
