#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/shape.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>

namespace chronon3d {

// Soft drop shadow drawn behind the shape.
// Simulated by N concentric layers of increasing spread and decreasing alpha.
struct DropShadow {
    bool  enabled{false};
    Vec2  offset{0.0f, 8.0f};          // pixel offset from the shape center
    Color color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   radius{12.0f};               // spread in pixels (controls softness)
};

// Soft glow emanating outward from the shape.
// Simulated by N concentric layers expanding from the shape edge.
struct Glow {
    bool  enabled{false};
    f32   radius{15.0f};               // expansion in pixels
    f32   intensity{0.8f};             // multiplied onto color alpha
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct RenderNode {
    std::pmr::string name;
    Transform world_transform;
    Color color{1, 1, 1, 1};
    Shape shape;
    DropShadow shadow;
    Glow glow;
    std::shared_ptr<Mesh> mesh;
    bool visible{true};

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : m_nodes(res) {}

    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    // Returns a mutable reference to the last added node.
    // Only call after at least one add_node().
    [[nodiscard]] RenderNode& last_node() { return m_nodes.back(); }

    [[nodiscard]] const std::pmr::vector<RenderNode>& nodes() const { return m_nodes; }
    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

private:
    std::pmr::vector<RenderNode> m_nodes;
};

} // namespace chronon3d
