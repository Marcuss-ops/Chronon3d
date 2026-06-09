#pragma once

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>

namespace chronon3d::runtime {

[[nodiscard]] graph::RenderGraph build_render_graph_from_scene(
    const Scene& scene,
    const graph::RenderGraphContext& ctx
);

} // namespace chronon3d::runtime
