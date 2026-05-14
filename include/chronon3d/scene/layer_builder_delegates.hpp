#pragma once

#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/builder_params.hpp>

namespace chronon3d {

class LayerBuilder;

class Layer3DDelegate {
public:
    static void add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p);
    static void add_fake_extruded_text(Layer& layer, std::string name, FakeExtrudedTextParams p);
    static void add_grid_plane(Layer& layer, std::string name, GridPlaneParams p);
};

} // namespace chronon3d
