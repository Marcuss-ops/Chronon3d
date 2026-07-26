#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/projector_2_5d.hpp>

namespace chronon3d {

// Renderer-owned parameters carried by a RenderNode, independent of runtime services.
struct FakeBox3DRenderState {
    renderer::ProjectionContext projection;
    Mat4 world_matrix{1.0f};
};

struct GridPlaneRenderState {
    renderer::ProjectionContext projection;
};

} // namespace chronon3d
