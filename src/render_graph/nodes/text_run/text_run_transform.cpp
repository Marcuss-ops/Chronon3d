// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode transform helper.
// See text_run_transform.hpp for the contract.

#include "text_run_transform.hpp"
#include "../../builder/graph_builder_coordinates.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::text_run {

glm::mat4 build_world_matrix(
    const RenderGraphContext& ctx,
    const TextRunPlacement& placement,
    const compiler::SampledLayerParameters* sampled_layer
) {
    // ── Stage 1: tight surfaces — surface-local basis only. ──
    // The producer framebuffer is a local [0,size) raster surface. Its
    // rasterizer already applies the run-local offset while composing the
    // glyph image, so the producer must only translate the authored local
    // origin into that surface. Applying placement.matrix here would
    // transform the text once in the producer and again in TransformNode.
    if (placement.tight_surface &&
        placement.surface_size.x > 0.0f &&
        placement.surface_size.y > 0.0f) {
        return glm::translate(
            glm::mat4(1.0f),
            glm::vec3(-placement.surface_origin.x,
                      -placement.surface_origin.y,
                      0.0f));
    }

    // ── Stage 2: canvas paths — SSAA over the pre-resolved placement. ──
    const glm::mat4 ssaa_scale = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));

    // The graph builder has already pre-computed the final world matrix
    // (including canvas-centre + 2.5D projection when applicable).
    // For the parentless screen-space canvas path, however, that matrix was
    // resolved from the build-frame pose. Re-compose the layer part from the
    // current sampled pose so layer tracks (position_y, scale, opacity) move
    // the raster instead of freezing on frame zero. Tight and projected
    // placements never opt in and keep their pre-resolved basis.
    glm::mat4 matrix = placement.matrix;
    if (placement.replace_layer_matrix && sampled_layer &&
        sampled_layer->active && sampled_layer->time_dependent) {
        glm::mat4 layer_matrix = sampled_layer->matrix;
        if (placement.canvas_centered) {
            layer_matrix = ::chronon3d::graph::detail::implicit_canvas_center_matrix(ctx) *
                           layer_matrix;
        }
        matrix = layer_matrix * placement.node_matrix;
    }
    // We only apply SSAA scaling on top — no other centering or projection
    // decisions happen here.
    return ssaa_scale * matrix;
}

}  // namespace chronon3d::graph::text_run
