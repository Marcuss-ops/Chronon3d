#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <stdexcept>

namespace chronon3d::graph {

void register_pipeline_graph_nodes() {
    auto& registry = GraphNodeRegistry::instance();

    if (registry.contains("source.precomp")) {
        return;  // already registered
    }

    registry.register_node(GraphNodeDescriptor{
        .id = "source.precomp",
        .display_name = "Precomposition",
        .description = "Renders a nested composition",
        .category = "source",
        .builtin = true,
        .factory = [](const GraphNodeCreateRequest& request)
            -> std::unique_ptr<RenderGraphNode>
        {
            const auto* spec =
                std::get_if<PrecompNodeCreateSpec>(&request.payload);

            if (!spec) {
                throw std::invalid_argument(
                    "source.precomp requires PrecompNodeCreateSpec");
            }

            return std::make_unique<PrecompNode>(
                spec->composition_name,
                spec->start_frame,
                spec->duration,
                spec->cache_frame,
                spec->cache_capacity,
                spec->tune_mode,
                spec->tune_interval,
                spec->tune_min_capacity,
                spec->tune_max_capacity
            );
        }
    });
}

} // namespace chronon3d::graph
