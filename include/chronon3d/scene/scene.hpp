#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <vector>
#include <memory>
#include <string>

namespace chronon3d {

struct RenderNode {
    std::string name;
    Transform world_transform;
    Color color{1, 1, 1, 1};
    std::shared_ptr<Mesh> mesh;
    bool visible{true};
};

class Scene {
public:
    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    [[nodiscard]] const std::vector<RenderNode>& nodes() const { return m_nodes; }

private:
    std::vector<RenderNode> m_nodes;
};

} // namespace chronon3d
