#pragma once

// Canonical spatial bounds for effects whose output can extend beyond their
// input.  Keep this contract next to the effect parameters so graph bounds,
// dirty tracking, and the software implementation cannot silently diverge.

#include <chronon3d/effects/effect_params.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::effects {

struct SpatialHalo {
    f32 left{0.0f};
    f32 top{0.0f};
    f32 right{0.0f};
    f32 bottom{0.0f};
};

[[nodiscard]] inline SpatialHalo drop_shadow_spatial_halo(
    const DropShadowParams& p) noexcept {
    // These values are the two maps used by effect_shadow_impl.cpp.  The
    // ambient map is deliberately wider and its offset is scaled by 1.65.
    // blur_padding_for_radius() contributes ceil(radius) + 2 pixels.
    const f32 contact_blur = std::max(1.0f, p.radius * 0.45f);
    const f32 ambient_blur = std::max(contact_blur + 4.0f, p.radius * 2.0f);
    const f32 blur_pad = std::max(
        1.0f, std::ceil(std::max(contact_blur, ambient_blur)) + 2.0f);

    const f32 contact_x = p.offset.x;
    const f32 contact_y = p.offset.y;
    const f32 ambient_x = p.offset.x * 1.65f;
    const f32 ambient_y = p.offset.y * 1.65f;
    return {
        blur_pad + std::max({0.0f, -contact_x, -ambient_x}),
        blur_pad + std::max({0.0f, -contact_y, -ambient_y}),
        blur_pad + std::max({0.0f, contact_x, ambient_x}),
        blur_pad + std::max({0.0f, contact_y, ambient_y}),
    };
}

[[nodiscard]] inline SpatialHalo offset_spatial_halo(
    const OffsetParams& p) noexcept {
    return {
        std::max(0.0f, -p.offset.x),
        std::max(0.0f, -p.offset.y),
        std::max(0.0f, p.offset.x),
        std::max(0.0f, p.offset.y),
    };
}

[[nodiscard]] inline SpatialHalo directional_blur_spatial_halo(
    const DirectionalBlurParams& p) noexcept {
    const f32 half_length = std::max(0.0f, p.length) * 0.5f;
    const f32 angle_radians = p.angle * (3.14159265358979323846f / 180.0f);
    const f32 dx = std::abs(std::cos(angle_radians)) * half_length;
    const f32 dy = std::abs(std::sin(angle_radians)) * half_length;
    // One pixel covers the linear-filter fringe used by the blur kernel.
    return {std::ceil(dx) + 1.0f, std::ceil(dy) + 1.0f,
            std::ceil(dx) + 1.0f, std::ceil(dy) + 1.0f};
}

} // namespace chronon3d::effects
