#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>

namespace chronon3d {

enum class NodeType {
    Mesh,
    Rect,
    Line,
    Circle
};

struct RenderNode {
    std::pmr::string name;
    NodeType type{NodeType::Rect};
    Transform world_transform;
    Vec3 size{1.0f, 1.0f, 1.0f}; // Rect: size, Circle: size.x = radius
    Vec3 line_end{0, 0, 0};      // For Lines
    Color color{1, 1, 1, 1};
    std::shared_ptr<Mesh> mesh;
    bool visible{true};

    RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : m_nodes(res) {}

    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    [[nodiscard]] const std::pmr::vector<RenderNode>& nodes() const { return m_nodes; }
    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

private:
    std::pmr::vector<RenderNode> m_nodes;
};

} // namespace chronon3d
