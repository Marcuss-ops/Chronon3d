#include <doctest/doctest.h>

#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>

#include "../../src/render_graph/builder/passes/graph_builder_source_pass.hpp"
#include "../../src/render_graph/pipeline/refresh/source.hpp"

#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace {

using namespace chronon3d;

Layer make_dust_layer_without_marker() {
    Layer layer;
    layer.name = "dust";
    layer.kind = LayerKind::Normal;
    // Keep one authored node so the source pass reaches the particle-marker
    // check; the node deliberately has no gpu_particle_instances payload.
    layer.nodes.emplace_back();
    return layer;
}

compiler::SampledLayerParameters make_sampled_dust() {
    compiler::SampledLayerParameters sampled;
    sampled.kind = compiler::NodeKind::Particles;
    sampled.active = true;
    sampled.elements.push_back(compiler::SampledElementParameters{});
    return sampled;
}

} // namespace

TEST_CASE("GPU particle marker: fresh graph build fails closed without a marker") {
    Layer layer = make_dust_layer_without_marker();
    auto sampled = make_sampled_dust();
    compiler::FrameParameterBlock block;
    block.layers.push_back(sampled);

    graph::RenderGraph graph;
    graph::RenderGraphContext ctx;
    ctx.policy.require_native_gpu = true;
    ctx.frame_input.width = 640;
    ctx.frame_input.height = 360;
    ctx.frame_parameters = &block;

    graph::LayerGraphItem item;
    item.layer = &layer;
    item.transform = layer.transform;

    graph::BuilderContext node_ctx;
    node_ctx.layer_index = 0;

    CHECK_THROWS_WITH_AS(
        graph::detail::append_source_pass(graph, item, ctx, node_ctx),
        "GPU particle layer 'dust' has no instance marker",
        std::runtime_error);
}

TEST_CASE("GPU particle marker: refresh fails closed without a marker") {
    Layer layer = make_dust_layer_without_marker();
    auto sampled = make_sampled_dust();

    ResolvedLayer resolved;
    resolved.layer = &layer;
    const std::unordered_map<std::string_view, const ResolvedLayer*> resolved_by_name{
        {std::string_view{"dust"}, &resolved}};
    const std::unordered_map<std::string_view, const RenderNode*> root_nodes;
    const std::unordered_map<std::string, bool> static_cache;

    graph::RenderGraphContext ctx;
    ctx.policy.require_native_gpu = true;
    graph::SourceNode source("dust_source", RenderNode{}, cache::NodeCacheKey{});
    source.set_layer_id("dust");

    CHECK_THROWS_WITH_AS(
        graph::detail::refresh_source_node(
            source, resolved_by_name, root_nodes, static_cache, ctx, nullptr, &sampled),
        "GPU particle layer 'dust' has no instance marker",
        std::runtime_error);

    // An empty sampled emitter has nothing to draw and must not be rejected:
    // the fail-closed condition is specifically a non-empty emitter whose
    // materialized marker disappeared.
    sampled.elements.clear();
    CHECK_NOTHROW(graph::detail::refresh_source_node(
        source, resolved_by_name, root_nodes, static_cache, ctx, nullptr, &sampled));
}
