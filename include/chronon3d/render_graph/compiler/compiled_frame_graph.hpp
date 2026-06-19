#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ── Binding metadata ───────────────────────────────────────────────────
// Attached to CompiledNodeInfo during graph build/compilation.
// The binding compiler reads this to build the binding table.
struct SceneBindingMetadata {
    bool     active{false};       // explicitly opt-in; avoids layer-0/item-0 ambiguity
    uint32_t layer_index{0};
    uint32_t item_index{0};
    uint16_t effect_begin{0};
    uint16_t effect_count{0};

    [[nodiscard]] bool has_binding() const { return active; }
};

struct CompiledNodeInfo {
    GraphNodeId id{k_invalid_node};

    RenderGraphNodeKind kind{};
    std::string name;
    std::string layer_id;

    std::vector<GraphNodeId> inputs;
    std::vector<GraphNodeId> consumers;

    cache::NodeCacheKey static_key{};
    RenderNodeCachePolicy cache_policy{};

    SceneBindingMetadata binding_meta{};  // binding table metadata

    bool reachable{false};
    bool frame_dependent{true};
    bool cacheable{false};
    bool disk_cacheable{false};
    bool early_exit_skip{false};

    std::optional<raster::BBox> predicted_bbox;
};

struct ResourceLifetime {
    GraphNodeId producer{k_invalid_node};
    std::size_t first_level{0};
    std::size_t last_level{0};
    std::size_t consumer_count{0};
    bool can_release_after_last_consumer{true};
};

struct CompiledFrameGraph {
    RenderGraph graph;
    GraphNodeId output{k_invalid_node};

    std::uint64_t structure_hash{0};

    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<std::size_t> consumer_counts;

    std::vector<CompiledNodeInfo> nodes;
    std::vector<ResourceLifetime> lifetimes;

    std::vector<bool> early_exit_skip;
    bool skip_initial_clear{false};

    bool valid{false};

    [[nodiscard]] bool empty() const {
        return !valid || levels.empty() || output == k_invalid_node;
    }
};

} // namespace chronon3d::graph
