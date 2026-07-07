// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode transform helper.
// See text_run_transform.hpp for the contract.

#include "text_run_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::text_run {

glm::mat4 build_world_matrix(
    const RenderGraphContext& ctx,
    const TextRunPlacement& placement
) {
    const glm::mat4 ssaa_scale = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));

    // The graph builder has already pre-computed the final world matrix
    // (including canvas-centre + 2.5D projection when applicable).
    // We only apply SSAA scaling on top — no centering or projection
    // decisions happen here.
    return ssaa_scale * placement.matrix;
}

}  // namespace chronon3d::graph::text_run
