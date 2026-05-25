#include "graph_builder_camera25d_sorter.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

void Camera25DLayerSorter::sort(std::vector<LayerGraphItem>& items) {
    spdlog::info("DEBUG SORT: Before sorting:");
    for (const auto& item : items) {
        spdlog::info("DEBUG SORT: name={} depth={:.2f} insertion_index={}", item.layer->name, item.depth, item.insertion_index);
    }
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.insertion_index < b.insertion_index;
        });
    spdlog::info("DEBUG SORT: After sorting:");
    for (const auto& item : items) {
        spdlog::info("DEBUG SORT: name={} depth={:.2f} insertion_index={}", item.layer->name, item.depth, item.insertion_index);
    }
}

} // namespace chronon3d::graph::detail
