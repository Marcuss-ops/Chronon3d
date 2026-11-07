#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace chronon3d::text::gel {

inline constexpr float kDefaultLightAzimuth = -2.176f;
// A low key light is what makes the dome read as a rounded tube. A nearly
// frontal light leaves N.H almost constant across the stroke and collapses
// the crest into the flat cyan face seen in the first smoke renders.
inline constexpr float kDefaultLightElevation = 0.38f;

// The Vulkan text-run ABI has one reserved uint32. Pack the light azimuth
// ([-pi,pi]) and normalized elevation ([0,1]) into two UNORM16 lanes so the
// per-run shader can sweep lighting without changing the 128-byte ABI.
[[nodiscard]] inline std::uint32_t pack_light_parameters(
    float azimuth_radians, float elevation) noexcept {
    constexpr float pi = std::numbers::pi_v<float>;
    const float azimuth = std::clamp(
        std::isfinite(azimuth_radians) ? azimuth_radians : kDefaultLightAzimuth,
        -pi, pi);
    const float z = std::clamp(
        std::isfinite(elevation) ? elevation : kDefaultLightElevation, 0.0f, 1.0f);
    const auto quantize = [](float value) {
        return static_cast<std::uint32_t>(std::lround(value * 65535.0f));
    };
    const std::uint32_t angle_bits = quantize((azimuth + pi) / (2.0f * pi));
    const std::uint32_t elevation_bits = quantize(z);
    return (elevation_bits << 16u) | angle_bits;
}

struct SurfaceSample {
    float height{};
    Vec3 normal{0.0f, 0.0f, 1.0f};
};

// Smooth union for the positive-inside signed-distance convention used by
// the text atlas. A union is max(a,b) for this sign convention (smooth-min
// would be correct for negative-inside fields). The stable form avoids
// overflow for large, distant fields.
[[nodiscard]] inline float smooth_union_distance(float a, float b,
                                                 float smoothness) noexcept {
    if (!(smoothness > 0.0f) || !std::isfinite(smoothness))
        return std::max(a, b);
    const float nearest = std::max(a, b);
    return nearest + std::log(std::exp(smoothness * (a - nearest)) +
                              std::exp(smoothness * (b - nearest))) /
                         smoothness;
}

[[nodiscard]] inline float rounded_height(float distance_inside,
                                         float radius) noexcept {
    if (!(radius > 0.0f) || !std::isfinite(radius) ||
        !std::isfinite(distance_inside) || distance_inside <= 0.0f)
        return 0.0f;
    const float t = std::clamp(distance_inside / radius, 0.0f, 1.0f);
    // Smootherstep dome: zero first and second derivative at the contour and
    // medial axis. This removes the bevel-like slope break where the interior
    // height field meets the exterior zero-height region.
    const float t2 = t * t;
    const float t3 = t2 * t;
    return radius * t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Derivative of rounded_height with respect to the inside distance. This is
// shared with normal reconstruction so the height oracle and surface gradient
// cannot drift apart.
[[nodiscard]] inline float rounded_slope(float distance_inside,
                                        float radius) noexcept {
    if (!(radius > 0.0f) || !std::isfinite(radius) ||
        !std::isfinite(distance_inside) || distance_inside <= 0.0f ||
        distance_inside >= radius)
        return 0.0f;
    const float t = distance_inside / radius;
    const float one_minus_t = 1.0f - t;
    return 30.0f * t * t * one_minus_t * one_minus_t;
}

// Fresnel alone vanishes on the smootherstep silhouette because its height
// slope is zero there. Add a narrow distance-field coat so the optical edge
// stays reflective while the analytic normal still controls the grazing term.
[[nodiscard]] inline float rim_response(float normal_z,
                                        float distance_inside,
                                        float edge_width,
                                        float exponent = 3.5f) noexcept {
    const float nz = std::clamp(std::isfinite(normal_z) ? normal_z : 1.0f,
                                0.0f, 1.0f);
    const float d = std::max(std::isfinite(distance_inside) ? distance_inside : 0.0f,
                             0.0f);
    const float width = std::isfinite(edge_width) ? edge_width : 0.0f;
    const float e = std::max(std::isfinite(exponent) ? exponent : 3.5f, 0.0f);
    const float fresnel = std::pow(1.0f - nz, e);
    if (!(width > 0.0f)) return fresnel;
    const float t = std::clamp((d - 0.05f * width) / (0.95f * width),
                               0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    const float edge_coat = 1.0f - smooth;
    return std::max(fresnel, 0.78f * edge_coat);
}

// Canonical dome radius for the bold display weight. 5.2 canonical em-texels
// covers the stroke half-width at the reference size while still scaling with
// minified/magnified glyphs.
inline constexpr float kDomeRadiusTexels = 5.2f;
inline constexpr float kMinDomeRadiusPx = 1.6f;
inline constexpr float kMaxDomeRadiusPx = 96.0f;

[[nodiscard]] inline float dome_radius_px(float texel_to_screen) noexcept {
    const float t = std::isfinite(texel_to_screen) ? texel_to_screen : 1.0f;
    return std::clamp(kDomeRadiusTexels * t, kMinDomeRadiusPx,
                      kMaxDomeRadiusPx);
}

[[nodiscard]] inline SurfaceSample reconstruct_surface(
    float distance_inside, Vec2 distance_gradient, float radius,
    float depth_scale = 1.0f) noexcept {
    SurfaceSample sample{};
    sample.height = rounded_height(distance_inside, radius);
    if (!(radius > 0.0f) || !(sample.height > 1e-5f) ||
        !std::isfinite(distance_gradient.x) ||
        !std::isfinite(distance_gradient.y) || !std::isfinite(depth_scale))
        return sample;

    const float dh_dd = rounded_slope(distance_inside, radius);
    const float nx = -depth_scale * dh_dd * distance_gradient.x;
    const float ny = -depth_scale * dh_dd * distance_gradient.y;
    const float inv_length = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
    sample.normal = Vec3{nx * inv_length, ny * inv_length, inv_length};
    return sample;
}

[[nodiscard]] inline float half_lambert(float normal_dot_light,
                                        float wrap = 0.3f) noexcept {
    const float w = std::clamp(std::isfinite(wrap) ? wrap : 0.0f, 0.0f, 1.0f);
    const float value = std::clamp((normal_dot_light + w) / (1.0f + w), 0.0f, 1.0f);
    return value * value;
}

[[nodiscard]] inline float geometric_roughness(
    float roughness, Vec3 normal_dx, Vec3 normal_dy) noexcept {
    const auto finite = [](float x) { return std::isfinite(x) ? x : 0.0f; };
    const float variance =
        finite(normal_dx.x) * finite(normal_dx.x) +
        finite(normal_dx.y) * finite(normal_dx.y) +
        finite(normal_dx.z) * finite(normal_dx.z) +
        finite(normal_dy.x) * finite(normal_dy.x) +
        finite(normal_dy.y) * finite(normal_dy.y) +
        finite(normal_dy.z) * finite(normal_dy.z);
    const float r = std::clamp(std::isfinite(roughness) ? roughness : 1.0f,
                               0.0f, 1.0f);
    return std::clamp(std::sqrt(r * r + variance), r, 1.0f);
}

[[nodiscard]] inline Color transmit(Color background, Color gel_color,
                                    Color absorption,
                                    float optical_thickness) noexcept {
    const float t = std::max(0.0f, std::isfinite(optical_thickness)
                                      ? optical_thickness : 0.0f);
    const auto channel = [t](float bg, float fluid, float sigma) {
        const float transmittance = std::exp(-std::max(0.0f, sigma) * t);
        return bg * transmittance + fluid * (1.0f - transmittance);
    };
    return Color{channel(background.r, gel_color.r, absorption.r),
                 channel(background.g, gel_color.g, absorption.g),
                 channel(background.b, gel_color.b, absorption.b),
                 background.a};
}

// Beer-Lambert attenuation for light travelling through the GEL body. This
// is deliberately separate from `transmit()`: the latter composites a
// background through a colored fluid, while this attenuates the material's
// lit surface color before specular/rim emission is added. Thickness is the
// normalized dome depth (zero at the silhouette, one at the medial axis).
[[nodiscard]] inline Color absorb(Color surface_color, Color absorption,
                                 float optical_thickness) noexcept {
    const float t = std::max(0.0f, std::isfinite(optical_thickness)
                                      ? optical_thickness : 0.0f);
    const auto channel = [t](float value, float sigma) {
        return value * std::exp(-std::max(0.0f, sigma) * t);
    };
    return Color{channel(surface_color.r, absorption.r),
                 channel(surface_color.g, absorption.g),
                 channel(surface_color.b, absorption.b),
                 surface_color.a};
}

} // namespace chronon3d::text::gel
