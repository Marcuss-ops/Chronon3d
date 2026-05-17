#pragma once

#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/projection_context.hpp>

namespace chronon3d {

// Forward-declared to avoid a math/ → scene/ include cycle.
// Full definition is in <chronon3d/scene/mask/mask.hpp>.
struct Mask;

struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 anchor{0.0f, 0.0f, 0.0f};
    f32 opacity{1.0f};

    constexpr Transform() = default;
    constexpr Transform(Vec3 p, Quat r = Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3 s = Vec3(1.0f), Vec3 a = Vec3(0.0f), f32 o = 1.0f)
        : position(p), rotation(r), scale(s), anchor(a), opacity(o) {}

    [[nodiscard]] Mat4 to_mat4() const {
        // Matrix = Translate(position) * Rotate(rotation) * Scale(scale) * Translate(-anchor)
        return math::translate(position) * math::rotate(rotation) * math::scale(scale) * math::translate(-anchor);
    }

    [[nodiscard]] Mat4 to_matrix() const { return to_mat4(); }

    [[nodiscard]] bool any() const {
        return position.x != 0.0f || position.y != 0.0f || position.z != 0.0f ||
               rotation.w != 1.0f || scale.x != 1.0f || scale.y != 1.0f || scale.z != 1.0f ||
               anchor.x != 0.0f || anchor.y != 0.0f || anchor.z != 0.0f ||
               opacity != 1.0f;
    }

    [[nodiscard]] bool is_identity_2d() const {
        return position.x == 0.0f && position.y == 0.0f && rotation.w == 1.0f && 
               scale.x == 1.0f && scale.y == 1.0f && anchor.x == 0.0f && anchor.y == 0.0f &&
               opacity == 1.0f;
    }
};

struct RenderState {
    Mat4 matrix;
    f32  opacity{1.0f};

    // 3D card rendering — set by SourceNode when layer.is_3d && camera_2_5d active.
    // Processors use projection to build ProjectedCard; world_matrix is layer TRS without camera.
    renderer::ProjectionContext projection{};
    Mat4 world_matrix{1.0f};
    bool is_3d_layer{false};

    // Set by the renderer when the layer has an active mask.
    // Both fields propagate from layer_state → node_state via combine().
    const Mask* mask{nullptr};
    Mat4 layer_inv_matrix{1.0f};
};

inline RenderState combine(const RenderState& parent, const Transform& child) {
    return RenderState{
        .matrix           = parent.matrix * child.to_matrix(),
        .opacity          = parent.opacity * child.opacity,
        .mask             = parent.mask,
        .layer_inv_matrix = parent.layer_inv_matrix,
    };
}

} // namespace chronon3d
