#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d {

// Forward-declared to avoid a math/ -> scene/ include cycle.
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
        return glm::translate(Mat4(1.0f), position) * glm::toMat4(rotation) * glm::scale(Mat4(1.0f), scale) * glm::translate(Mat4(1.0f), -anchor);
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

[[nodiscard]] inline Transform from_mat4(const Mat4& matrix, f32 opacity = 1.0f) {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 skew{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 perspective{0.0f, 0.0f, 0.0f, 0.0f};

    Mat4 local = matrix;
    if (!glm::decompose(local, scale, rotation, translation, skew, perspective)) {
        Transform out;
        out.opacity = opacity;
        return out;
    }

    Transform out;
    out.position = translation;
    out.rotation = glm::normalize(rotation);
    out.scale = scale;
    out.anchor = {0.0f, 0.0f, 0.0f};
    out.opacity = opacity;
    return out;
}

[[nodiscard]] inline Transform combine_transforms(const Transform& parent, const Transform& child) {
    const Mat4 world = parent.to_mat4() * child.to_mat4();
    return from_mat4(world, parent.opacity * child.opacity);
}

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
    mutable std::shared_ptr<Framebuffer> mask_alpha_cache;
    mutable std::uint64_t mask_alpha_cache_key{0};

    std::string layer_id;
    std::optional<raster::BBox> clip_rect;
};

inline void ensure_mask_alpha_cache(const RenderState& state, i32 width, i32 height) {
    if (!state.mask || !state.mask->enabled()) {
        state.mask_alpha_cache.reset();
        state.mask_alpha_cache_key = 0;
        return;
    }

    const std::uint64_t key = hash_mask_cache_key(*state.mask, state.layer_inv_matrix, width, height);
    if (state.mask_alpha_cache && state.mask_alpha_cache_key == key) {
        return;
    }

    state.mask_alpha_cache = rasterize_mask_alpha(*state.mask, state.layer_inv_matrix, width, height);
    state.mask_alpha_cache_key = key;
}

inline RenderState combine(const RenderState& parent, const Transform& child) {
    return RenderState{
        .matrix           = parent.matrix * child.to_matrix(),
        .opacity          = parent.opacity * child.opacity,
        .mask             = parent.mask,
        .layer_inv_matrix = parent.layer_inv_matrix,
        .mask_alpha_cache = parent.mask_alpha_cache,
        .mask_alpha_cache_key = parent.mask_alpha_cache_key,
        .layer_id         = parent.layer_id,
        .clip_rect        = parent.clip_rect,
    };
}

} // namespace chronon3d
