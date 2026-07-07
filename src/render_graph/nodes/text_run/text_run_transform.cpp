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

    // Single return path — the graph builder has already resolved
    // the matrix, including canvas-centre offsets for centered layers.
    glm::mat4 result;
    if (uses_2_5d_projection || centered) {
        if (matrix_override) {
            // When matrix_override is present, the source pass already
            // supplies a canvas-space matrix (canvas-center was applied
            // by the source pass for implicit-centered layers, so
            // `m_matrix_override` is in canvas-space coordinates).
            // Applying `canvas_center` here would be a redundant second
            // translation that pushes text off-canvas (text bbox ends up
            // centred at (2880, 1620) instead of (1920, 1080) for
            // 1920×1080).  Skip it.
            result = ssaa_scale * (*matrix_override);
        } else {
            result = canvas_center
                   * ssaa_scale
                   * render_ref.world_transform.to_mat4();
        }
    } else {
        result = ssaa_scale
               * matrix_override.value_or(render_ref.world_transform.to_mat4());
    }

    return result;
}

}  // namespace chronon3d::graph::text_run
