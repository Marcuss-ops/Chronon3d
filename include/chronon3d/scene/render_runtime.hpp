#pragma once

#include <chronon3d/math/mat4.hpp>

namespace chronon3d {

struct FakeBox3DRenderState {
    bool cam_ready{false};
    Mat4 cam_view{1.0f};
    Mat4 world_matrix{1.0f};  // layer TRS — transforms local vertices to world space
    f32 cam_focal{1000.0f};
    f32 vp_cx{640.0f};
    f32 vp_cy{360.0f};
};

struct GridPlaneRenderState {
    bool cam_ready{false};
    Mat4 cam_view{1.0f};
    f32 cam_focal{1000.0f};
    f32 vp_cx{640.0f};
    f32 vp_cy{360.0f};
};

struct FakeExtrudedTextRenderState {
    bool cam_ready{false};
    Mat4 cam_view{1.0f};
    f32 cam_focal{1000.0f};
    f32 vp_cx{640.0f};
    f32 vp_cy{360.0f};
    // Layer world transform (rotation/position/scale before projection).
    // Used to place glyph vertices in world space before cam_view.
    Mat4 world_matrix{1.0f};
};

} // namespace chronon3d
