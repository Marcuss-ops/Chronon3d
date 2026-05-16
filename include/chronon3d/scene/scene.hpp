#pragma once

#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <vector>
#include <memory_resource>

namespace chronon3d {

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : m_nodes(res), m_layers(res) {}

    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    void add_layer(Layer layer) {
        m_layers.push_back(std::move(layer));
    }

    [[nodiscard]] RenderNode& last_node() { return m_nodes.back(); }

    [[nodiscard]] const std::pmr::vector<RenderNode>& nodes() const { return m_nodes; }
    [[nodiscard]] const std::pmr::vector<Layer>& layers() const { return m_layers; }

    void set_camera_2_5d(Camera2_5DRuntime camera) { m_camera_2_5d = camera; }
    [[nodiscard]] const Camera2_5DRuntime& camera_2_5d() const { return m_camera_2_5d; }

    void resolve_hierarchy(Frame frame);
    [[nodiscard]] bool hierarchy_baked() const { return m_hierarchy_baked; }
    [[nodiscard]] bool hierarchy_resolved() const { return m_hierarchy_baked; }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

private:
    std::pmr::vector<RenderNode> m_nodes;
    std::pmr::vector<Layer> m_layers;
    Camera2_5DRuntime m_camera_2_5d{};
    bool m_hierarchy_baked{false};
};

} // namespace chronon3d
