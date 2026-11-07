#pragma once

// Production shadow-map contract for the Vulkan compute backend.
//
// Chronon3D rasterizes in compute (see inline_mesh.comp), so the shadow pass is
// expressed in the same terms: caster triangles are projected on the host into
// light-map pixel space, the caster kernel keeps the nearest depth per texel,
// and the lighting-side kernel samples that map with PCF and an adaptive bias.
//
// This header owns the CPU-visible half of the contract so the authoring layer,
// the backend and the tests share one definition of the light matrices, the
// diffusion radius and the bias rule.

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/light_context.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chronon3d::rendering {

/// Authored shadow-map controls. `diffusion_px` is the After Effects Shadow
/// Diffusion radius in shadow-map pixels; it widens the PCF kernel without
/// changing the stored depth, which keeps the contact edge stable.
struct ShadowMapSettings {
    bool enabled{false};
    std::uint32_t resolution{1024};
    std::uint32_t pcf_taps{3};
    float pcf_radius_px{1.0f};
    float diffusion_px{0.0f};
    float depth_bias{0.0025f};
    float normal_bias{0.01f};
    float strength{1.0f};
};

/// A light-space transform plus the direction used by the bias rule.
struct ShadowMapLight {
    Mat4 view_projection{1.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f};
    bool valid{false};
};

[[nodiscard]] inline ShadowMapLight build_directional_shadow_map(
    Vec3 travel_direction, Vec3 scene_center, float scene_radius,
    float depth_range = 1.0f) {
    ShadowMapLight out;
    const float radius = std::max(scene_radius, 1.0e-3f);
    const float padding = std::max(depth_range, 0.0f);
    const Vec3 direction = safe_normalize(travel_direction, Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 up = std::abs(direction.z) > 0.9f
        ? Vec3{0.0f, 1.0f, 0.0f}
        : Vec3{0.0f, 0.0f, 1.0f};
    const Vec3 eye = scene_center - direction * (radius + padding);
    const Mat4 view = glm::lookAtLH(eye, scene_center, up);
    // Left-handed, zero-to-one depth: the projected z already matches the
    // Vulkan clip-space depth the kernels compare.
    const Mat4 projection = glm::orthoLH_ZO(
        -radius, radius, -radius, radius, 0.0f, 2.0f * radius + padding);
    out.view_projection = projection * view;
    out.direction = direction;
    out.valid = true;
    return out;
}

[[nodiscard]] inline ShadowMapLight build_spot_shadow_map(
    Vec3 position, Vec3 axis, float half_angle_radians,
    float near_plane, float far_plane) {
    ShadowMapLight out;
    const Vec3 direction = safe_normalize(axis, Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 up = std::abs(direction.z) > 0.9f
        ? Vec3{0.0f, 1.0f, 0.0f}
        : Vec3{0.0f, 0.0f, 1.0f};
    const float fov = std::clamp(2.0f * half_angle_radians, 1.0e-3f, 3.1f);
    const float near_z = std::max(near_plane, 1.0e-4f);
    const float far_z = std::max(far_plane, near_z + 1.0e-3f);
    const Mat4 view = glm::lookAtLH(position, position + direction, up);
    const Mat4 projection = glm::perspectiveLH_ZO(fov, 1.0f, near_z, far_z);
    out.view_projection = projection * view;
    out.direction = direction;
    out.valid = true;
    return out;
}

/// Effective PCF radius. Diffusion is authored in pixels and adds to the fixed
/// kernel radius; the clamp keeps a pathological plan from expanding the tap
/// loop beyond the 5x5 kernel both the kernel and the CPU reference implement.
[[nodiscard]] inline float shadow_map_pcf_radius(
    const ShadowMapSettings& settings) noexcept {
    return std::clamp(settings.pcf_radius_px + settings.diffusion_px,
                      0.0f, 4.0f * 5.0f);
}

/// Normal-dependent bias: at grazing angles the receiver and caster can share a
/// texel, so the bias grows with the slope. `to_light` points from the surface
/// towards the light.
[[nodiscard]] inline float shadow_map_adaptive_bias(
    const ShadowMapSettings& settings, Vec3 normal, Vec3 to_light) noexcept {
    const float ndl = std::clamp(
        glm::dot(safe_normalize(normal), safe_normalize(to_light)), 0.0f, 1.0f);
    const float slope = std::sqrt(std::max(0.0f, 1.0f - ndl * ndl)) /
                        std::max(ndl, 0.1f);
    return settings.depth_bias + settings.normal_bias * std::min(slope, 4.0f);
}

struct ShadowProjectedPoint {
    float x{0.0f};
    float y{0.0f};
    float depth{0.0f};
    bool valid{false};
};

/// Project a world point into shadow-map pixel space. `depth` is the
/// zero-to-one light-space depth stored by the caster kernel.
[[nodiscard]] inline ShadowProjectedPoint project_shadow_point(
    const Mat4& view_projection, Vec3 world, std::uint32_t resolution) {
    ShadowProjectedPoint out;
    const Vec4 clip = view_projection * Vec4{world, 1.0f};
    if (!std::isfinite(clip.w) || clip.w <= 1.0e-6f) return out;
    const float inverse_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inverse_w;
    const float ndc_y = clip.y * inverse_w;
    const float ndc_z = clip.z * inverse_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z))
        return out;
    const float u = ndc_x * 0.5f + 0.5f;
    const float v = ndc_y * 0.5f + 0.5f;
    out.depth = ndc_z;
    out.x = u * static_cast<float>(resolution);
    out.y = v * static_cast<float>(resolution);
    out.valid = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f &&
                ndc_z >= 0.0f && ndc_z <= 1.0f;
    return out;
}

/// Push-constant block shared byte-for-byte with shadow_apply.comp (128 bytes).
struct alignas(16) ShadowApplyParams {
    Mat4 light_view_projection{1.0f};
    Vec4 normal_layer_z{0.0f, 0.0f, 1.0f, 0.0f};
    Vec4 origin_size{0.0f, 0.0f, 0.0f, 0.0f};
    // x = depth bias, y = normal bias, z = PCF radius px, w = taps
    Vec4 bias_radius_taps{0.0f, 0.0f, 1.0f, 3.0f};
    // xyz = direction towards the light, w = enabled
    Vec4 light_direction_enabled{0.0f, 0.0f, -1.0f, 1.0f};
};
static_assert(sizeof(ShadowApplyParams) == 128,
              "ShadowApplyParams must match shadow_apply.comp push constants");

[[nodiscard]] inline ShadowApplyParams make_shadow_apply_params(
    const ShadowMapLight& light, const ShadowMapSettings& settings,
    Vec3 normal, float layer_z, int origin_x, int origin_y) {
    ShadowApplyParams out;
    out.light_view_projection = light.view_projection;
    out.normal_layer_z = Vec4{normal, layer_z};
    out.origin_size = Vec4{static_cast<float>(origin_x),
                           static_cast<float>(origin_y), 0.0f, 0.0f};
    const float taps = static_cast<float>(std::clamp<std::uint32_t>(
        settings.pcf_taps == 0 ? 1 : settings.pcf_taps, 1, 5));
    out.bias_radius_taps = Vec4{settings.depth_bias, settings.normal_bias,
                                shadow_map_pcf_radius(settings), taps};
    out.light_direction_enabled =
        Vec4{light.direction, settings.enabled && light.valid ? 1.0f : 0.0f};
    return out;
}

/// CPU reference that mirrors shadow_apply.comp. The depth map is an RGBA32F
/// image where r = stored depth and g >= 0.5 marks a covered texel.
[[nodiscard]] inline float shadow_factor_cpu(
    const std::vector<float>& depth_map_rgba, std::uint32_t width,
    std::uint32_t height, const Mat4& light_view_projection,
    Vec3 world_position, Vec3 world_normal, Vec3 to_light,
    const ShadowMapSettings& settings) {
    if (!settings.enabled || width == 0 || height == 0 ||
        depth_map_rgba.size() < static_cast<std::size_t>(width) * height * 4U)
        return 1.0f;
    // The CPU reference takes the same zero-to-one matrix as the kernel.
    ShadowProjectedPoint receiver;
    {
        const Vec4 clip = light_view_projection * Vec4{world_position, 1.0f};
        if (!std::isfinite(clip.w) || clip.w <= 1.0e-6f) return 1.0f;
        const float inverse_w = 1.0f / clip.w;
        const float u = clip.x * inverse_w * 0.5f + 0.5f;
        const float v = clip.y * inverse_w * 0.5f + 0.5f;
        const float z = clip.z * inverse_w;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 1.0f;
        receiver = ShadowProjectedPoint{u, v, z, true};
    }
    const float bias = shadow_map_adaptive_bias(settings, world_normal, to_light);
    const int taps = static_cast<int>(std::clamp<std::uint32_t>(
        settings.pcf_taps == 0 ? 1 : settings.pcf_taps, 1, 5));
    const float radius = shadow_map_pcf_radius(settings);
    float lit = 0.0f;
    float total = 0.0f;
    for (int ty = 0; ty < taps; ++ty) {
        for (int tx = 0; tx < taps; ++tx) {
            float offset_x = 0.0f;
            float offset_y = 0.0f;
            if (taps > 1) {
                const float denom = static_cast<float>(taps - 1);
                offset_x = (static_cast<float>(tx) / denom * 2.0f - 1.0f) * radius;
                offset_y = (static_cast<float>(ty) / denom * 2.0f - 1.0f) * radius;
            }
            const float sample_u = std::clamp(
                receiver.x + offset_x / static_cast<float>(width), 0.0f, 1.0f);
            const float sample_v = std::clamp(
                receiver.y + offset_y / static_cast<float>(height), 0.0f, 1.0f);
            const auto texel_x = std::clamp(
                static_cast<int>(sample_u * static_cast<float>(width)),
                0, static_cast<int>(width) - 1);
            const auto texel_y = std::clamp(
                static_cast<int>(sample_v * static_cast<float>(height)),
                0, static_cast<int>(height) - 1);
            const std::size_t index =
                (static_cast<std::size_t>(texel_y) * width +
                 static_cast<std::size_t>(texel_x)) * 4U;
            const float stored_depth = depth_map_rgba[index];
            const bool covered = depth_map_rgba[index + 1U] >= 0.5f;
            const bool occluded = covered && (receiver.depth - bias) > stored_depth;
            lit += occluded ? 0.0f : 1.0f;
            total += 1.0f;
        }
    }
    const float visibility = total > 0.0f ? lit / total : 1.0f;
    return 1.0f - std::clamp(settings.strength, 0.0f, 1.0f) *
                      (1.0f - visibility);
}

} // namespace chronon3d::rendering
