#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

class RenderGraphNode;
struct GraphNodeCreateRequest;

/// Descriptor for a render graph node type that can be registered.
struct GraphNodeDescriptor {
    std::string id;            // unique identifier (e.g. "per_pixel_dof", "custom_blur")
    std::string display_name;  // human-readable name
    std::string description;   // short description for docs / introspection
    std::string category;      // grouping (e.g. "transform", "effect", "source", "composite")
    bool        builtin{false};

    /// Parameterized factory — receives the creation request.
    using NodeFactory = std::function<
        std::unique_ptr<RenderGraphNode>(const GraphNodeCreateRequest&)
    >;
    NodeFactory factory{};
};

/// Domain-specific catalog for render graph node types.
///
/// Features and modules can register new node types here without modifying
/// core graph files.  The catalog is a plain value object — create one,
/// populate it, freeze it, and pass it to the graph compiler.
///
///     GraphNodeCatalog catalog;
///     catalog.register_node({...});
///     catalog.register_node({...});
///     catalog.freeze();
///
///     GraphCompiler compiler{.nodes = catalog, ...};
///
class GraphNodeCatalog {
public:
    GraphNodeCatalog() = default;

    /// Register a node type descriptor.  The `id` must be unique.
    /// Must be called before freeze().
    void register_node(GraphNodeDescriptor descriptor);

    /// Prevent further registrations.  After freeze(), register_node()
    /// throws.  Call this once registration is complete.
    void freeze();

    /// Check if a node with the given id is registered.
    [[nodiscard]] bool contains(std::string_view id) const noexcept;

    /// Find a descriptor by id (returns nullptr if not found).
    [[nodiscard]] const GraphNodeDescriptor* find(std::string_view id) const noexcept;

    /// Get a descriptor by id (throws if not found).
    [[nodiscard]] const GraphNodeDescriptor& get(std::string_view id) const;

    /// Get all registered node ids.
    [[nodiscard]] std::vector<std::string> available() const;

    /// Get all registered descriptors.
    [[nodiscard]] std::vector<GraphNodeDescriptor> list() const;

    /// Get all registered descriptors in a given category.
    [[nodiscard]] std::vector<GraphNodeDescriptor> list_by_category(std::string_view category) const;

    /// Create a node instance by id with an empty request.
    [[nodiscard]] std::unique_ptr<RenderGraphNode> create(std::string_view id) const;

    /// Create a node instance by id with a parameterized request.
    /// Returns nullptr if not found or no factory.
    [[nodiscard]] std::unique_ptr<RenderGraphNode> create(
        std::string_view id, const GraphNodeCreateRequest& request) const;

    /// Clear all registered nodes (resets frozen state).
    void clear();

private:
    std::map<std::string, GraphNodeDescriptor, std::less<>> m_nodes;
    bool m_frozen{false};
};

// Backward-compatible alias — migrate to GraphNodeCatalog.
using GraphNodeRegistry = GraphNodeCatalog;

} // namespace chronon3d::graph
