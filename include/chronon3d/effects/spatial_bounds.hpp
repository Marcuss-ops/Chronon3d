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

[[nodiscard]] inline SpatialHalo uniform_spatial_halo(f32 extent) noexcept {
    const f32 safe_extent = std::max(0.0f, extent);
    return {safe_extent, safe_extent, safe_extent, safe_extent};
}

[[nodiscard]] inline f32 spatial_halo_extent(const SpatialHalo& halo) noexcept {
    return std::max({halo.left, halo.top, halo.right, halo.bottom});
}

/// Conservative support radius for the software blur implementation. The
/// renderer adaptively downsamples large radii (and can recursively downsample
/// again); compute the support in the same strategy tree, scale it back to
/// source pixels, and reserve fringe for the box downsample + bilinear upscale.
/// `passes` follows apply_blur()'s contract (which clamps to at least one).
[[nodiscard]] inline SpatialHalo blur_spatial_halo(
    f32 radius, int passes = 2) noexcept {
    if (radius <= 0.5f) return {};
    // apply_blur() clamps the pass count to at least one. Keep the predicted
    // support on that exact contract for zero/negative caller values too.
    passes = std::max(1, passes);

    const auto support_at_full_resolution = [passes](f32 value) noexcept {
        const int rounded_radius = std::max(
            1, static_cast<int>(std::round(std::max(0.0f, value))));
        int pass_radius = rounded_radius;
        if (passes == 1) {
            pass_radius = std::max(
                1, static_cast<int>(std::ceil(std::max(0.0f, value))));
        } else if (passes == 2) {
            const f32 r_plus = static_cast<f32>(rounded_radius) *
                               static_cast<f32>(rounded_radius + 1);
            pass_radius = std::max(1, static_cast<int>(std::round(
                0.5f * (std::sqrt(1.0f + 6.0f * r_plus) - 1.0f))));
        }
        return static_cast<f32>(passes * pass_radius) + 2.0f;
    };

    f32 support = 0.0f;
    f32 scale = 1.0f;
    f32 scaled_radius = radius;
    while (scaled_radius > 10.0f) {
        const f32 factor = scaled_radius <= 24.0f ? 2.0f : 4.0f;
        // One conservative source-space fringe allowance per adaptive
        // downsample/upscale stage. This also covers nested stages.
        support += scale * (2.0f * factor);
        scale *= factor;
        scaled_radius /= factor;
    }
    support += scale * support_at_full_resolution(scaled_radius);
    return uniform_spatial_halo(support);
}

/// The HQ glow uses a Gaussian with sigma=radius/3 and a 3-sigma kernel;
/// the extra four pixels conservatively cover the visible tail/fringe.
[[nodiscard]] inline SpatialHalo glow_spatial_halo(f32 radius) noexcept {
    return uniform_spatial_halo(std::ceil(std::max(0.0f, radius) * 4.0f) + 4.0f);
}

[[nodiscard]] inline SpatialHalo effect_spatial_halo(
    const BlurParams& p) noexcept {
    return blur_spatial_halo(p.radius, 2);
}

[[nodiscard]] inline SpatialHalo effect_spatial_halo(
    const GlowParams& p) noexcept {
    return glow_spatial_halo(p.radius);
}

[[nodiscard]] inline SpatialHalo effect_spatial_halo(
    const BloomParams& p) noexcept {
    return blur_spatial_halo(p.radius, 1);
}

[[nodiscard]] inline f32 drop_shadow_blur_padding(
    const DropShadowParams& p) noexcept {
    const f32 contact_blur = std::max(1.0f, p.radius * 0.45f);
    const f32 ambient_blur = std::max(contact_blur + 4.0f, p.radius * 2.0f);
    // The largest of the two shadow maps controls their shared ROI. The
    // contact radius can exceed the ambient radius for unusual negative
    // radii/parameter values, so take the union rather than assuming ambient
    // is always wider.
    return std::max(
        spatial_halo_extent(blur_spatial_halo(contact_blur, 1)),
        spatial_halo_extent(blur_spatial_halo(ambient_blur, 1)));
}

[[nodiscard]] inline SpatialHalo drop_shadow_spatial_halo(
    const DropShadowParams& p) noexcept {
    // These values are the two maps used by effect_shadow_impl.cpp. The
    // ambient map is deliberately wider and its offset is scaled by 1.65.
    const f32 blur_pad = drop_shadow_blur_padding(p);
    // Match effect_shadow_impl.cpp exactly: the authored offset is rounded
    // once to integer contact pixels, then the ambient offset is scaled from
    // that rounded value and rounded again. Using raw floats here can
    // underpredict a fractional offset (e.g. 0.4px rounds to a visible 1px).
    const f32 contact_x = std::round(p.offset.x);
    const f32 contact_y = std::round(p.offset.y);
    const f32 ambient_x = std::round(contact_x * 1.65f);
    const f32 ambient_y = std::round(contact_y * 1.65f);
    return {
        blur_pad + std::max({0.0f, -contact_x, -ambient_x}),
        blur_pad + std::max({0.0f, -contact_y, -ambient_y}),
        blur_pad + std::max({0.0f, contact_x, ambient_x}),
        blur_pad + std::max({0.0f, contact_y, ambient_y}),
    };
}

[[nodiscard]] inline SpatialHalo effect_spatial_halo(
    const DropShadowParams& p) noexcept {
    return drop_shadow_spatial_halo(p);
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
