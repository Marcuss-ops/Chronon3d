#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/runtime/graph_executor.hpp>
#include <spdlog/spdlog.h>

#include <chrono>

namespace chronon3d::software_internal {

namespace {

graph::RenderGraphContext make_graph_context(SoftwareRenderer& renderer, const Camera& camera,
                                             i32 width, i32 height, Frame frame,
                                             f32 frame_time) {
    return graph::RenderGraphContext{
        .frame = frame,
        .time_seconds = frame_time,
        .width = width,
        .height = height,
        .camera = camera,
        .renderer = &renderer,
        .node_cache = &renderer.node_cache(),
        .registry = renderer.composition_registry(),
        .video_decoder = renderer.video_decoder(),
        .ssaa_factor = renderer.render_settings().ssaa_factor,
        .modular_coordinates = renderer.render_settings().use_modular_graph
    };
}

} // namespace

std::unique_ptr<Framebuffer> render_scene_internal(SoftwareRenderer& renderer,
                                                   const Scene& scene, const Camera& camera,
                                                   i32 width, i32 height, Frame frame,
                                                   f32 frame_time) {
    ZoneScoped;
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = renderer.node_cache().stats().hits;

    const auto t_build0 = std::chrono::steady_clock::now();
    auto ctx = make_graph_context(renderer, camera, width, height, frame, frame_time);
    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d,
            ctx.width,
            ctx.height
        );
        ctx.projection_ctx.ready = true;
    }
    graph::RenderGraph graph = graph::GraphBuilder::build(scene, ctx);
    const auto t_build1 = std::chrono::steady_clock::now();

    const auto t_exec0 = std::chrono::steady_clock::now();
    graph::GraphExecutor executor;
    auto fb_shared = executor.execute(graph, graph.output(), ctx);
    const auto t_exec1 = std::chrono::steady_clock::now();
    const auto hits_after = renderer.node_cache().stats().hits;

    const auto t1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry({
        .event = "scene_render",
        .frame = frame,
        .width = width,
        .height = height,
        .total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
        .setup_ms = std::chrono::duration<double, std::milli>(t_build1 - t_build0).count(),
        .composite_ms = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count(),
        .cache_hit = hits_after > hits_before ? 1 : 0,
        .layer_count = static_cast<int>(scene.layers().size()),
    });
    return std::make_unique<Framebuffer>(*fb_shared);
}

std::string debug_render_graph(const SoftwareRenderer& renderer, const Scene& scene,
                               const Camera& camera, i32 width, i32 height, Frame frame,
                               f32 frame_time) {
    auto ctx = make_graph_context(const_cast<SoftwareRenderer&>(renderer), camera, width, height,
                                  frame, frame_time);
    return graph::GraphBuilder::build(scene, ctx).to_dot();
}

} // namespace chronon3d::software_internal
