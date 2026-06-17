#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

#include <ranges>
#include <stdexcept>

namespace chronon3d::graph {

GraphNodeRegistry& GraphNodeRegistry::instance() {
    static GraphNodeRegistry s_instance;
    return s_instance;
}

GraphNodeRegistry::GraphNodeRegistry() = default;
GraphNodeRegistry::~GraphNodeRegistry() = default;

void GraphNodeRegistry::register_node(GraphNodeDescriptor descriptor) {
    if (descriptor.id.empty()) {
        throw std::invalid_argument("GraphNodeRegistry::register_node: empty id");
    }
    auto [it, inserted] = m_nodes.try_emplace(descriptor.id, std::move(descriptor));
    if (!inserted) {
        throw std::invalid_argument(
            "GraphNodeRegistry::register_node: duplicate id '" + it->first + "'");
    }
}

bool GraphNodeRegistry::contains(std::string_view id) const {
    return m_nodes.find(id) != m_nodes.end();
}

const GraphNodeDescriptor& GraphNodeRegistry::get(std::string_view id) const {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) {
        throw std::out_of_range(
            std::string("GraphNodeRegistry::get: unknown id '") +
            std::string(id) + "'");
    }
    return it->second;
}

std::vector<std::string> GraphNodeRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_nodes.size());
    std::ranges::copy(m_nodes | std::views::keys, std::back_inserter(ids));
    return ids;
}

std::vector<GraphNodeDescriptor> GraphNodeRegistry::list() const {
    std::vector<GraphNodeDescriptor> result;
    result.reserve(m_nodes.size());
    std::ranges::copy(m_nodes | std::views::values, std::back_inserter(result));
    return result;
}

std::vector<GraphNodeDescriptor> GraphNodeRegistry::list_by_category(std::string_view category) const {
    auto filtered = m_nodes
                  | std::views::values
                  | std::views::filter([category](const GraphNodeDescriptor& desc) {
                        return desc.category == category;
                    });
    return {filtered.begin(), filtered.end()};
}

std::unique_ptr<RenderGraphNode> GraphNodeRegistry::create(std::string_view id) const {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end() || !it->second.factory) {
        return nullptr;
    }
    return it->second.factory();
}

void GraphNodeRegistry::clear() {
    m_nodes.clear();
}

} // namespace chronon3d::graph
