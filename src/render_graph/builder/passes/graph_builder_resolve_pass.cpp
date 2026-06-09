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
    ctx.cam25d = ctx.resolved.camera.camera;

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
