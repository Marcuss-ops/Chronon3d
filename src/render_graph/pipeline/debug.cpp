#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "helpers.hpp"

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
    media::MediaFrameProvider* video_decoder,
    float fps
) {
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );
    
    ctx.frame_input.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.frame_input.camera_2_5d = resolved_camera.camera;
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.projection_ctx = renderer::make_projection_context(
            ctx.frame_input.camera_2_5d,
            ctx.frame_input.width,
            ctx.frame_input.height
        );
        ctx.frame_input.projection_ctx.ready = true;
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
    media::MediaFrameProvider* video_decoder,
    bool execute,
    bool include_dot,
    float fps
) {
    SceneGraphStats stats;
    stats.scene_layers = static_cast<int>(scene.layers().size());

    const auto t0 = profiling::now();

    auto ctx = make_graph_context(backend, node_cache, camera, width, height,
                                   frame, frame_time, settings, registry, video_decoder, fps);
    ctx.frame_input.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.frame_input.camera_2_5d  = resolved_camera.camera;
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.projection_ctx = renderer::make_projection_context(ctx.frame_input.camera_2_5d, ctx.frame_input.width, ctx.frame_input.height);
        ctx.frame_input.projection_ctx.ready = true;
    }

    const auto t_build0 = profiling::now();
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    const auto t_build1 = profiling::now();

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
        if (n.cache_policy().enabled()) ++stats.cacheable_nodes;
    }

    if (include_dot) stats.dot = graph.to_dot();

    stats.build_ms = profiling::duration_ms(t_build0, t_build1);

    if (execute && graph.has_output()) {
    const auto t_exec0 = profiling::now();
    GraphExecutor executor;
    RenderSession session;
    ExecutionScheduler scheduler{SchedulerMode::Sequential, 1, false};
    // PR-2 rewire — compile the raw graph through FrameGraphCompiler first
    // (single source of truth for topological plans after WP-8 close-out).
    FrameGraphCompiler compiler;
    auto compiled = compiler.compile(std::move(graph), ctx);
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("execute_graph", trace_category::kGraph);
        // PR 6.1 — migrate to execute_with_scope (typed ExecutionScope contract)
        ExecutionScope root_scope(ExecutionScopeKind::Root, session,
                                  compiled.graph_instance_id);
        fb_shared = executor.execute_with_scope(compiled, ctx, root_scope, scheduler);
    }
    const auto t_exec1 = profiling::now();
        stats.execute_ms   = profiling::duration_ms(t_exec0, t_exec1);
    }

    stats.total_ms = profiling::duration_ms(t0, profiling::now());
    return stats;
}

} // namespace chronon3d::graph
