#pragma once

#include <chronon3d/timeline/layer.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <memory>

namespace chronon3d {

class MeshLayer : public Layer {
public:
    MeshLayer(std::string name, std::shared_ptr<Mesh> mesh) 
        : Layer(std::move(name), LayerType::Mesh), m_mesh(std::move(mesh)) {}

    [[nodiscard]] std::shared_ptr<Mesh> mesh() const { return m_mesh; }

private:
    std::shared_ptr<Mesh> m_mesh;
};

} // namespace chronon3d
