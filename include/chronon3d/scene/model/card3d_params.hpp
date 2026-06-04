#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/card3d_material.hpp>

namespace chronon3d {

// Builder parameters for a Card3D — a fake 3D card composed of a front face,
// side quads, and edge highlights, rendered without a real mesh.
//
// Used with LayerBuilder::card3d() to create a card with physical depth.
struct Card3DParams {
    Vec2  size{280.0f, 180.0f};    // front face dimensions
    f32   radius{16.0f};            // corner radius (for front face)
    Color color{0.08f, 0.08f, 0.16f, 0.92f}; // front face base color
    Vec3  pos{0.0f, 0.0f, 0.0f};   // position

    Card3DMaterial material{};      // card material (thickness, edge, rim, etc.)
};

} // namespace chronon3d
