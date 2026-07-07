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

/// TICKET-TEXT-CLEANUP-5: centralized centering.
/// The source pass now always provides the resolved matrix (including
/// canvas-center for centered layers).  build_world_matrix just applies
/// SSAA scale — no centering decision here.
glm::mat4 build_world_matrix(
    const ::chronon3d::RenderNode& render_ref,
    const RenderGraphContext& ctx,
    bool uses_2_5d_projection,
    std::optional<glm::mat4> matrix_override
);

}  // namespace chronon3d::graph::text_run
