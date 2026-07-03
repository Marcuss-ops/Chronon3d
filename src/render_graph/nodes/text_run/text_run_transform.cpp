// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode transform helper.
// See text_run_transform.hpp for the contract.

#include "text_run_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::text_run {

glm::mat4 build_world_matrix(
    const ::chronon3d::RenderNode& render_ref,
    const RenderGraphContext& ctx,
    bool uses_2_5d_projection,
    bool centered,
    std::optional<glm::mat4> matrix_override
) {
    const glm::mat4 ssaa_scale = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const glm::mat4 canvas_center = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            ctx.frame_input.width * 0.5f,
            ctx.frame_input.height * 0.5f,
            0.0f));

    if (uses_2_5d_projection || centered) {
        return canvas_center
             * ssaa_scale
             * matrix_override.value_or(render_ref.world_transform.to_mat4());
    }
    return ssaa_scale
         * matrix_override.value_or(render_ref.world_transform.to_mat4());
}

}  // namespace chronon3d::graph::text_run
