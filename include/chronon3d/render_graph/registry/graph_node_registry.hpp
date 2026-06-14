#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

class RenderGraphNode;

/// Descriptor for a render graph node type that can be registered.
struct GraphNodeDescriptor {
    std::string id;            // unique identifier (e.g. "per_pixel_dof", "custom_blur")
    std::string display_name;  // human-readable name
    std::string description;   // short description for docs / introspection
    std::string category;      // grouping (e.g. "transform", "effect", "source", "composite")
    bool        builtin{false};

    using NodeFactory = std::function<std::unique_ptr<RenderGraphNode>()>;
    NodeFactory factory{};
};

/// Domain-specific registry for render graph node types.
///
/// Features and modules can register new node types here without modifying
/// core graph files.  The registry is used by the node factory / pipeline
/// to instantiate nodes by id.
///
/// Singleton — use `GraphNodeRegistry::instance()`.
class GraphNodeRegistry {
public:
    static GraphNodeRegistry& instance();

    GraphNodeRegistry();
    ~GraphNodeRegistry();

    /// Register a node type descriptor.  The `id` must be unique.
    void register_node(GraphNodeDescriptor descriptor);

    /// Check if a node with the given id is registered.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Get a descriptor by id (throws if not found).
    [[nodiscard]] const GraphNodeDescriptor& get(std::string_view id) const;

    /// Get all registered node ids.
    [[nodiscard]] std::vector<std::string> available() const;

    /// Get all registered descriptors.
    [[nodiscard]] std::vector<GraphNodeDescriptor> list() const;

    /// Get all registered descriptors in a given category.
    [[nodiscard]] std::vector<GraphNodeDescriptor> list_by_category(std::string_view category) const;

    /// Create a node instance by id (returns nullptr if not found or no factory).
    [[nodiscard]] std::unique_ptr<RenderGraphNode> create(std::string_view id) const;

    /// Clear all registered nodes (used in tests for clean reset).
    void clear();

private:
    std::map<std::string, GraphNodeDescriptor, std::less<>> m_nodes;
};

} // namespace chronon3d::graph
