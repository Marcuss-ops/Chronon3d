#include "graph_builder_camera25d_sorter.hpp"

#include <algorithm>

namespace chronon3d::graph::detail {

void Camera25DLayerSorter::sort(std::vector<LayerGraphItem>& items) {
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            return a.depth > b.depth;
        });
}

} // namespace chronon3d::graph::detail
