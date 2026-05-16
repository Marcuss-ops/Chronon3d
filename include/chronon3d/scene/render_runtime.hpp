#pragma once

#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/mat4.hpp>

namespace chronon3d {

struct FakeBox3DRenderState {
    renderer::ProjectionContext projection;
    Mat4 world_matrix{1.0f};  // layer TRS — transforms local vertices to world space
};

struct GridPlaneRenderState {
    renderer::ProjectionContext projection;
};

struct FakeExtrudedTextRenderState {
    renderer::ProjectionContext projection;
    // Layer world transform (rotation/position/scale before projection).
    // Used to place glyph vertices in world space before cam_view.
    Mat4 world_matrix{1.0f};
};

} // namespace chronon3d
