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

    // Refactored to a single return so the OUTPUT matrix translation
    // can be logged alongside the inputs (TICKET-104 diagnostic).
    glm::mat4 result;
    const char* branch = "non-centered";
    if (uses_2_5d_projection || centered) {
        if (matrix_override) {
            // TICKET-104 — when matrix_override is present, the source
            // pass already supplies a canvas-space matrix (canvas-center
            // was applied by the source pass for implicit-centered
            // layers, so `m_matrix_override` is in canvas-space
            // coordinates).  Applying `canvas_center` here would be a
            // redundant second translation that pushes text off-canvas
            // (text bbox ends up centered at (2880, 1620) instead of
            // (1920, 1080) for 1920×1080).  Skip it.
            result = ssaa_scale * (*matrix_override);
            branch = "centered+override-skip-canvas";
        } else {
            result = canvas_center
                   * ssaa_scale
                   * render_ref.world_transform.to_mat4();
            branch = "centered+no-override-apply-canvas";
        }
    } else {
        result = ssaa_scale
               * matrix_override.value_or(render_ref.world_transform.to_mat4());
    }

    // TICKET-104 DIAGNOSTIC — one-shot runtime log of inputs + OUTPUT
    // matrix translation.  *** REMOVE once root cause is confirmed. ***
    // This is the canonical matrix composition site for TextRunNode;
    // comparing it against SourceNode::predicted_bbox (separate TICKET-104
    // diagnostic) determines whether the +offset bug is in the shared
    // matrix composition path or TextRunNode-specific.
    static bool kTicket104BwmOneShot = true;
    if (kTicket104BwmOneShot) {
        spdlog::info(
            "[TICKET104:build_world_matrix] branch={} override.has={} "
            "override.t=({:.2f},{:.2f}) render_ref.t=({:.2f},{:.2f}) "
            "centered={} uses_2_5d={} ssaa={:.2f} "
            "result.t=({:.2f},{:.2f}) canvas=({:.0f},{:.0f})",
            branch,
            matrix_override.has_value() ? 1 : 0,
            matrix_override ? (*matrix_override)[3][0] : -1.0f,
            matrix_override ? (*matrix_override)[3][1] : -1.0f,
            render_ref.world_transform.position.x,
            render_ref.world_transform.position.y,
            centered ? 1 : 0,
            uses_2_5d_projection ? 1 : 0,
            static_cast<f32>(ctx.policy.ssaa_factor),
            result[3][0], result[3][1],
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height)
        );
        kTicket104BwmOneShot = false;
    }

    return result;
}

}  // namespace chronon3d::graph::text_run
