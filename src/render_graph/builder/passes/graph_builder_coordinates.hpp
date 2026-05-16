#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

inline bool should_use_centered_rendering(const LayerGraphItem& item,
                                          const RenderGraphContext& ctx) {
    return item.layer
        && item.layer->kind == LayerKind::Normal
        && !item.projected
        && !ctx.modular_coordinates;
}

inline Transform calculate_centered_transform(const Transform& transform,
                                              const RenderGraphContext&) {
    return transform;
}

} // namespace chronon3d::graph::detail
