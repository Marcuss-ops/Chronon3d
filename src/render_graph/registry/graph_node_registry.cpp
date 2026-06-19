#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

#include <ranges>
#include <stdexcept>

namespace chronon3d::graph {

void GraphNodeCatalog::register_node(GraphNodeDescriptor descriptor) {
    if (m_frozen) {
        throw std::logic_error("GraphNodeCatalog::register_node: catalog is frozen");
    }
    if (descriptor.id.empty()) {
        throw std::invalid_argument("GraphNodeCatalog::register_node: empty id");
    }
    auto [it, inserted] = m_nodes.try_emplace(descriptor.id, std::move(descriptor));
    if (!inserted) {
        throw std::invalid_argument(
            "GraphNodeCatalog::register_node: duplicate id '" + it->first + "'");
    }
}

void GraphNodeCatalog::freeze() {
    m_frozen = true;
}

bool GraphNodeCatalog::contains(std::string_view id) const noexcept {
    return m_nodes.find(id) != m_nodes.end();
}

const GraphNodeDescriptor* GraphNodeCatalog::find(std::string_view id) const noexcept {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return nullptr;
    return &it->second;
}

const GraphNodeDescriptor& GraphNodeCatalog::get(std::string_view id) const {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) {
        throw std::out_of_range(
            std::string("GraphNodeCatalog::get: unknown id '") +
            std::string(id) + "'");
    }
    return it->second;
}

std::vector<std::string> GraphNodeCatalog::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_nodes.size());
    std::ranges::copy(m_nodes | std::views::keys, std::back_inserter(ids));
    return ids;
}

std::vector<GraphNodeDescriptor> GraphNodeCatalog::list() const {
    std::vector<GraphNodeDescriptor> result;
    result.reserve(m_nodes.size());
    std::ranges::copy(m_nodes | std::views::values, std::back_inserter(result));
    return result;
}

std::vector<GraphNodeDescriptor> GraphNodeCatalog::list_by_category(std::string_view category) const {
    auto filtered = m_nodes
                  | std::views::values
                  | std::views::filter([category](const GraphNodeDescriptor& desc) {
                        return desc.category == category;
                    });
    return {filtered.begin(), filtered.end()};
}

std::unique_ptr<RenderGraphNode> GraphNodeCatalog::create(std::string_view id) const {
    return create(id, GraphNodeCreateRequest{});
}

std::unique_ptr<RenderGraphNode> GraphNodeCatalog::create(
    std::string_view id, const GraphNodeCreateRequest& request) const
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end() || !it->second.factory) {
        return nullptr;
    }
    return it->second.factory(request);
}

} // namespace chronon3d::graph
