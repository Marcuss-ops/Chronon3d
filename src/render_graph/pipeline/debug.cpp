#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "helpers.hpp"
#include <chrono>

namespace chronon3d::graph {

std::string debug_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    float fps
) {
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );
    
    ctx.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.camera_2_5d = resolved_camera.camera;
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d,
            ctx.width,
            ctx.height
        );
        ctx.projection_ctx.ready = true;
    }
    
    return GraphBuilder::build(scene, ctx).to_dot();
}

SceneGraphStats analyze_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width, i32 height,
    Frame frame, f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    bool execute,
    bool include_dot,
    float fps
) {
    SceneGraphStats stats;
    stats.scene_layers = static_cast<int>(scene.layers().size());

    const auto t0 = std::chrono::steady_clock::now();

    auto ctx = make_graph_context(backend, node_cache, camera, width, height,
                                   frame, frame_time, settings, registry, video_decoder, fps);
    ctx.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.camera_2_5d  = resolved_camera.camera;
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(ctx.camera_2_5d, ctx.width, ctx.height);
        ctx.projection_ctx.ready = true;
    }

    const auto t_build0 = std::chrono::steady_clock::now();
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    const auto t_build1 = std::chrono::steady_clock::now();

    stats.total_nodes    = graph.size();
    stats.output_id      = graph.has_output() ? graph.output() : k_invalid_node;
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        const RenderGraphNode& n = graph.node(i);
        switch (n.kind()) {
            case RenderGraphNodeKind::Source:     ++stats.source_nodes;      break;
            case RenderGraphNodeKind::Transform:  ++stats.transform_nodes;   break;
            case RenderGraphNodeKind::Effect:     ++stats.effect_nodes;      break;
            case RenderGraphNodeKind::Mask:       ++stats.mask_nodes;        break;
            case RenderGraphNodeKind::Composite:  ++stats.composite_nodes;   break;
            case RenderGraphNodeKind::Adjustment: ++stats.adjustment_nodes;  break;
            case RenderGraphNodeKind::Precomp:    ++stats.precomp_nodes;     break;
            case RenderGraphNodeKind::Video:      ++stats.video_nodes;       break;
            case RenderGraphNodeKind::MotionBlur: ++stats.motion_blur_nodes; break;
            default:                              ++stats.other_nodes;       break;
        }
        if (n.cacheable()) ++stats.cacheable_nodes;
    }

    if (include_dot) stats.dot = graph.to_dot();

    stats.build_ms = std::chrono::duration<double, std::milli>(t_build1 - t_build0).count();

    if (execute && graph.has_output()) {
        stats.cache_before = node_cache.stats();
    const auto t_exec0 = std::chrono::steady_clock::now();
    GraphExecutor executor;
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("execute_graph", trace_category::kGraph);
        fb_shared = executor.execute(graph, graph.output(), ctx);
    }
    const auto t_exec1 = std::chrono::steady_clock::now();
        stats.cache_after  = node_cache.stats();
        stats.execute_ms   = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count();
    }

    stats.total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return stats;
}

} // namespace chronon3d::graph
