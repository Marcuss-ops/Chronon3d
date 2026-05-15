#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/runtime/graph_executor.hpp>

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

    auto ctx = make_graph_context(renderer, camera, width, height, frame, frame_time);
    graph::RenderGraph graph = graph::GraphBuilder::build(scene, ctx);
    graph::GraphExecutor executor;
    auto fb_shared = executor.execute(graph, graph.output(), ctx);
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
