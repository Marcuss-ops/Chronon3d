#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>

namespace chronon3d::rendering {

// A 2.5D layer projected into screen space: four corners + per-corner UVs + depth.
// Produced by ProjectionContext::project_card().
// Used by Image, Rect, Text, and specialized renderers to render flat layers in 3D space.
//
// NOTE: restored in the migration-debt sweep. The removal commit (ceac97481,
// "remove legacy projected card rendering model") dropped this header while
// its rasterizers (projected_card_rasterizer / card3d_material_rasterizer),
// the live scene-model Card3DMaterial (Layer::card3d_material) and their
// test suites were still present — a half migration. The struct is restored
// so the 2.5D card rasterizer surface is coherent again.
struct ProjectedCard {
    Vec3 corners[4]{};               // screen-space corners: TL, TR, BR, BL (z = camera-space depth)
    Vec2 uvs[4]{                     // texture coordinates matching corners
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {1.0f, 1.0f}, {0.0f, 1.0f}
    };
    f32  depth{0.0f};                // average view-space depth (for z-sorting)
    bool visible{false};             // false if any corner is behind the camera
};

} // namespace chronon3d::rendering
