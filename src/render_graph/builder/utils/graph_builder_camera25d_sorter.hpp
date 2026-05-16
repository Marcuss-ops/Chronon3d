#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

struct Camera25DLayerSorter {
    static void sort(std::vector<LayerGraphItem>& items);
};

} // namespace chronon3d::graph::detail
