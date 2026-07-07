// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode transform helper.
// See text_run_transform.hpp for the contract.

#include "text_run_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

glm::mat4 build_world_matrix(
    const ::chronon3d::RenderNode& render_ref,
    const RenderGraphContext& ctx,
    bool uses_2_5d_projection,
    std::optional<glm::mat4> matrix_override
) {
    const glm::mat4 ssaa_scale = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));

    // TICKET-TEXT-CLEANUP-5: centralized centering.
    // The source pass now always provides the resolved matrix
    // (including canvas-center for centered layers).  TextRunNode
    // no longer decides centering — it just applies SSAA.
    //
    // For 2.5D projection without a pre-resolved matrix (should not
    // happen in practice — the source pass always provides one),
    // fall back to the render ref's world transform.
    return ssaa_scale
         * matrix_override.value_or(render_ref.world_transform.to_mat4());
}

}  // namespace chronon3d::graph::text_run
