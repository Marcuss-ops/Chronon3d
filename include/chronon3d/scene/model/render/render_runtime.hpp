#pragma once

#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

struct FakeBox3DRenderState {
    renderer::ProjectionContext projection;
    Mat4 world_matrix{1.0f};  // layer TRS — transforms local vertices to world space
};

struct GridPlaneRenderState {
    renderer::ProjectionContext projection;
};

} // namespace chronon3d
