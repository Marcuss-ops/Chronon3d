#pragma once

#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>
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

    void set_camera_2_5d(Camera2_5D camera) { m_camera_2_5d = camera; }
    [[nodiscard]] const Camera2_5D& camera_2_5d() const { return m_camera_2_5d; }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

private:
    std::pmr::vector<RenderNode> m_nodes;
    std::pmr::vector<Layer> m_layers;
    Camera2_5D m_camera_2_5d{};
};

} // namespace chronon3d
