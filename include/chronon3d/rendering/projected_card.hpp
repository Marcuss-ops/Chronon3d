#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/core/types.hpp>

namespace chronon3d::rendering {

// A 2.5D layer projected into screen space: four corners + per-corner UVs + depth.
// Produced by ProjectionContext::project_card().
// Used by Image, Rect, Text, and specialized renderers to render flat layers in 3D space.
struct ProjectedCard {
    Vec2 corners[4]{};               // screen-space corners: TL, TR, BR, BL
    Vec2 uvs[4]{                     // texture coordinates matching corners
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {1.0f, 1.0f}, {0.0f, 1.0f}
    };
    f32  depth{0.0f};                // average view-space depth (for z-sorting)
    bool visible{false};             // false if any corner is behind the camera
};

} // namespace chronon3d::rendering
