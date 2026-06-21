// ---------------------------------------------------------------------------
// preflight_render_graph.cpp
//
// Implementation of debug_preflight_render_graph().
//
// Strategy:
//   1. Build the render graph exactly as debug_scene_graph() does (no execute).
//   2. Delegate to analysis phases in preflight_render_graph_analysis.cpp.
//   3. Produce an enriched DOT string.
//   4. Fill and return the GraphPreflightReport.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include "format.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "../pipeline/helpers.hpp"
#include "analysis.hpp"

namespace chronon3d::graph {

// ── Main implementation ────────────────────────────────────────────────────

GraphPreflightReport debug_preflight_render_graph(
    RenderBackend&             backend,
    cache::NodeCache&          node_cache,
    const Scene&               scene,
    const Camera&              camera,
    i32 width,
    i32 height,
    Frame  frame,
    f32    frame_time,
    const RenderSettings&      settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider*  video_decoder,
    float fps
) {
    // ── 1. Build context (same as debug_scene_graph) ─────────────────────
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height,
        frame, frame_time, settings, registry, video_decoder, fps
    );
    ctx.policy.diagnostics_enabled = true;
    ctx.frame_input.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.frame_input.camera_2_5d      = resolved_camera.camera;
        ctx.frame_input.has_camera_2_5d  = true;
        ctx.frame_input.projection_ctx   = renderer::make_projection_context(ctx.frame_input.camera_2_5d, width, height);
        ctx.frame_input.projection_ctx.ready = true;
    }

    if (auto* sw_backend = dynamic_cast<SoftwareBackend*>(&backend)) {
        (void)sw_backend;  // R3b follow-up: forward graph_cache/node_catalog/effect_catalog/runtime.resolver via SoftwareBackend.
    }

    // ── 2. Build graph (no execution) ────────────────────────────────────
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    const raster::BBox canvas{0, 0, width, height};
    const int64_t canvas_area = static_cast<int64_t>(width) * static_cast<int64_t>(height);

    // Count output edges up front
    std::vector<int> output_degree(graph.size(), 0);
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        if (!graph.has_node(i)) continue;
        for (GraphNodeId inp : graph.inputs(i)) {
            if (inp < static_cast<GraphNodeId>(output_degree.size())) {
                ++output_degree[inp];
            }
        }
    }

    GraphPreflightReport report;

    // ── 3–6. Delegated analysis phases ───────────────────────────────────
    populate_node_basics(graph, ctx, output_degree, canvas, canvas_area, report);
    auto topo_order = topo_sort_and_peak_memory(graph, output_degree, report);
    compute_aggregate_metrics(graph, canvas_area, report);

    // ── 7–10. Further analysis phases ────────────────────────────────────
    check_topological_warnings(graph, report);
    if (ctx.services.asset_resolver) {
        // Local-reference guard: when the SoftwareRenderer branch above
        // populated ctx.services.asset_resolver, asset integrity runs
        // through that exact resolver instance.  When no
        // SoftwareRenderer backend was wired (test fixtures, hypothetical
        // non-SWR backends, etc.) we skip MISSING_ASSET checks with a
        // clear warning so the report isn't fully silent.  Closes the
        // CRITICAL null-pointer deref flagged by the PR-8.0 review.
        chronon3d::assets::AssetResolver& resolver = *ctx.services.asset_resolver;
        check_asset_integrity(graph, resolver, report);
    } else {
        report.warnings.push_back(
            "ASSET_RESOLVER_UNAVAILABLE: ctx.services.asset_resolver is null; "
            "skipping MISSING_ASSET checks. Wire a SoftwareRenderer-backed "
            "preflight call site (or supply an AssetResolver) to enable "
            "asset integrity.");
    }
    check_coordinate_mismatch(graph, topo_order, report);
    propagate_dirty_chain(graph, topo_order, report);

    // ── 11. Build enriched DOT ───────────────────────────────────────────
    report.dot = build_enriched_dot(graph, ctx, report.nodes);

    return report;
}

} // namespace chronon3d::graph
