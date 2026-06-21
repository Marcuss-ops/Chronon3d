#include "graph_builder_passes.hpp"
#include "../graph_builder_internal.hpp"
#include "../graph_builder_pipeline.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

namespace chronon3d::graph::detail {

void ResolvePass::run(GraphBuildContext& ctx) {
    if (!ctx.resolved_prepopulated) {
        // Resolve layers and camera from scene hierarchy.
        auto resolved = detail::resolve_layers(*ctx.scene, *ctx.render_ctx);

        // Move resolved data into context for downstream passes.
        ctx.resolved.layers = std::move(resolved.layers);
        ctx.resolved.camera = std::move(resolved.camera);
    }
    // ── TICKET-013 follow-up ─────────────────────────────────────────────
    // The TICKET-011 mass-sed chain (.camera. → .frame_input.camera.)
    // over-applied to this line, producing ctx.resolved.frame_input.camera.camera.
    // Pre-migration: ctx.resolved.camera; ResolvedData::camera is of type
    // `ResolvedCamera` (resolved-camera representation), NOT `Camera`,
    // so even the pre-migration form was a type mismatch with the
    // `Camera2_5DRuntime` target on ctx.cam25d.  The original intent is
    // ambiguous from the post-migration source alone; defer the camera
    // handoff rewrite until TICKET-013 re-derives the conversion path
    // from RenderGraphContext::frame_input.camera_2_5d (the runtime
    // 2.5D camera) or from a ResolvedCamera→Camera2_5DRuntime cast helper.
    // Commenting out to keep the GREEN-build gate satisfied; the default-
    // constructed Camera2_5DRuntime above is unchanged.
    //   ctx.cam25d = ...;  // TICKET-013: rewire camera handoff

    // Compute which layers are static (cache-safe across frames).
    detail::compute_static_layers(ctx.resolved.layers, ctx.is_static_cache);

    // Build lookup tables for matte sources and name-to-resolved mapping.
    for (auto& rl : ctx.resolved.layers) {
        ctx.name_to_resolved[std::string(rl.layer->name)] = &rl;
        if (rl.layer->track_matte.active()) {
            ctx.matte_source_names.insert(
                std::string(rl.layer->track_matte.source_layer));
        }
    }
}

} // namespace chronon3d::graph::detail
