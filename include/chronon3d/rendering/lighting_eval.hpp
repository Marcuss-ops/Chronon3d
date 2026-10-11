#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace chronon3d::rendering {

using namespace chronon3d::graph;

[[nodiscard]] inline Vec3 transform_normal(const Mat4& matrix, Vec3 normal) {
    const Mat4 normal_matrix = glm::transpose(glm::inverse(matrix));
    Vec3 out{
        normal_matrix[0][0] * normal.x + normal_matrix[1][0] * normal.y + normal_matrix[2][0] * normal.z,
        normal_matrix[0][1] * normal.x + normal_matrix[1][1] * normal.y + normal_matrix[2][1] * normal.z,
        normal_matrix[0][2] * normal.x + normal_matrix[1][2] * normal.y + normal_matrix[2][2] * normal.z,
    };
    return safe_normalize(out);
}

[[nodiscard]] inline Color evaluate_lighting(
    Color base,
    Vec3 normal_world,
    const LightContext& light,
    const Material2_5D& material,
    const RimLight* rim_override = nullptr
) {
    if (!light.enabled || !material.accepts_lights) {
        return base;
    }

    const Vec3 n = safe_normalize(normal_world);
    const Vec3 l = safe_normalize(light.direction);
    const Vec3 v = {0.0f, 0.0f, 1.0f};
    const Vec3 h = safe_normalize(l + v, v);

    const f32 ndotl = std::max(0.0f, glm::dot(n, l));
    const f32 ndoth = std::max(0.0f, glm::dot(n, h));
    const f32 rim_ndotv = std::clamp(glm::dot(n, v), 0.0f, 1.0f);

    // Legacy rim derived from specular (kept for backwards compatibility)
    const f32 rim_spec = light.directional_enabled
        ? std::pow(1.0f - rim_ndotv, 2.2f) * material.specular * 0.18f
        : 0.0f;

    const f32 ambient = light.ambient_enabled
        ? std::max(0.0f, light.ambient * material.ambient_multiplier)
        : 0.0f;
    const f32 diffuse = light.directional_enabled
        ? std::max(0.0f, light.diffuse * material.diffuse * ndotl)
        : 0.0f;
    const f32 specular = light.directional_enabled && material.specular > 0.0f
        ? std::max(0.0f, light.diffuse * material.specular * std::pow(ndoth, std::max(1.0f, material.shininess)))
        : 0.0f;

    // Independent rim light (RimLight struct overrides the specular-derived rim)
    const f32 rim_power = (rim_override && rim_override->enabled)
        ? rim_override->power
        : 2.2f;
    const f32 rim_intensity = (rim_override && rim_override->enabled)
        ? rim_override->intensity
        : rim_spec;
    const Color rim_color = (rim_override && rim_override->enabled)
        ? rim_override->color
        : light.directional_color;
    const f32 rim_factor = std::pow(1.0f - rim_ndotv, rim_power);
    const f32 rim_total = rim_factor * rim_intensity;

    const f32 r = std::clamp(
        ambient * light.ambient_color.r +
        diffuse * light.directional_color.r +
        specular * 0.98f +
        rim_total * rim_color.r * 0.6f,
        0.0f, 1.0f);
    const f32 g = std::clamp(
        ambient * light.ambient_color.g +
        diffuse * light.directional_color.g +
        specular * 0.98f +
        rim_total * rim_color.g * 0.6f,
        0.0f, 1.0f);
    const f32 b = std::clamp(
        ambient * light.ambient_color.b +
        diffuse * light.directional_color.b +
        specular * 0.98f +
        rim_total * rim_color.b * 0.6f,
        0.0f, 1.0f);

    return Color{base.r * r, base.g * g, base.b * b, base.a};
}

// Irradiance (RGB, not clamped) that one per-pixel light deposits on a point of
// a planar 2.5D layer with normal `normal_world`. Closed form, no state.
[[nodiscard]] inline Color evaluate_point_light_irradiance(
    Vec3 normal_world, Vec3 pixel_world, const PointLightSource& source,
    const Material2_5D& material) {
    const Vec3 n = safe_normalize(normal_world);
    const Vec3 axis = safe_normalize(source.direction, Vec3{0.0f, 0.0f, -1.0f});
    Vec3 to_light{0.0f, 0.0f, 1.0f};
    f32 attenuation = 1.0f;
    if (source.kind == PointLightKind::Directional) {
        to_light = -axis;
    } else {
        const Vec3 delta = source.position - pixel_world;
        const f32 distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (source.range > 0.0f && distance > source.range) return Color{0.0f, 0.0f, 0.0f, 1.0f};
        if (distance > 1e-6f) to_light = delta / distance;
        const f32 scale = std::max(source.radius, 1e-6f);
        attenuation = 1.0f / (1.0f + std::pow(distance / scale, std::max(0.0f, source.decay)));
        if (source.kind == PointLightKind::Spot) {
            // Cosine between the light->pixel direction and the spot axis.
            const f32 cos_to_pixel = glm::dot(-to_light, axis);
            const f32 cos_outer = std::cos(source.angle);
            const f32 cos_inner = std::cos(source.angle * (1.0f - source.penumbra));
            f32 cone = 1.0f;
            if (cos_to_pixel <= cos_outer) {
                cone = 0.0f;
            } else if (cos_to_pixel < cos_inner) {
                const f32 t = (cos_to_pixel - cos_outer) / (cos_inner - cos_outer);
                cone = t * t * (3.0f - 2.0f * t);
            }
            attenuation *= cone;
        }
    }
    const f32 k = attenuation * std::max(0.0f, glm::dot(n, to_light)) * material.diffuse;
    return Color{source.color.r * k, source.color.g * k, source.color.b * k, 1.0f};
}

// Per-pixel lighting. Without point lights this is exactly evaluate_lighting().
// With point lights the scene term (when the scene light is enabled) is summed
// with each light's irradiance and clamped; when the scene light is disabled
// the point lights are the only light, so unlit regions go black.
[[nodiscard]] inline Color evaluate_lighting_at(
    Color base,
    Vec3 normal_world,
    Vec3 pixel_world,
    const LightContext& light,
    const Material2_5D& material,
    const RimLight* rim_override = nullptr
) {
    if (!material.accepts_lights || light.point_lights.empty()) {
        return evaluate_lighting(base, normal_world, light, material, rim_override);
    }
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    if (light.enabled) {
        const Color scene = evaluate_lighting(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                              normal_world, light, material, rim_override);
        r = scene.r;
        g = scene.g;
        b = scene.b;
    }
    for (const auto& source : light.point_lights) {
        const Color irradiance = evaluate_point_light_irradiance(
            normal_world, pixel_world, source, material);
        r += irradiance.r;
        g += irradiance.g;
        b += irradiance.b;
    }
    return Color{base.r * std::clamp(r, 0.0f, 1.0f),
                 base.g * std::clamp(g, 0.0f, 1.0f),
                 base.b * std::clamp(b, 0.0f, 1.0f), base.a};
}

[[nodiscard]] inline u64 hash_point_light(const PointLightSource& s) {
    u64 seed = hash_value(static_cast<std::uint8_t>(s.kind));
    seed = hash_combine(seed, hash_vec3(s.position));
    seed = hash_combine(seed, hash_vec3(s.direction));
    seed = hash_combine(seed, hash_color(s.color));
    seed = hash_combine(seed, hash_value(s.radius));
    seed = hash_combine(seed, hash_value(s.range));
    seed = hash_combine(seed, hash_value(s.decay));
    seed = hash_combine(seed, hash_value(s.angle));
    seed = hash_combine(seed, hash_value(s.penumbra));
    seed = hash_combine(seed, hash_value(s.casts_shadow));
    seed = hash_combine(seed, hash_value(s.shadow_radius));
    seed = hash_combine(seed, hash_value(s.shadow_bias));
    return seed;
}

[[nodiscard]] inline u64 hash_shadow_settings(const ShadowSettings& s) {
    u64 seed = hash_value(s.opacity);
    seed = hash_combine(seed, hash_value(s.blur_radius));
    seed = hash_combine(seed, hash_value(s.px_per_unit));
    seed = hash_combine(seed, hash_value(s.max_offset));
    seed = hash_combine(seed, hash_value(s.contact_opacity));
    seed = hash_combine(seed, hash_value(s.contact_blur_radius));
    seed = hash_combine(seed, hash_value(s.ambient_opacity));
    seed = hash_combine(seed, hash_value(s.ambient_blur_radius));
    seed = hash_combine(seed, hash_value(s.depth_aware));
    seed = hash_combine(seed, hash_value(s.blur_per_z));
    seed = hash_combine(seed, hash_value(s.opacity_falloff));
    seed = hash_combine(seed, hash_color(s.tint));
    return seed;
}

[[nodiscard]] inline u64 hash_light_context(const LightContext& light) {
    u64 seed = hash_value(light.enabled);
    seed = hash_combine(seed, hash_value(light.ambient_enabled));
    seed = hash_combine(seed, hash_value(light.directional_enabled));
    seed = hash_combine(seed, hash_vec3(light.direction));
    seed = hash_combine(seed, hash_color(light.ambient_color));
    seed = hash_combine(seed, hash_value(light.ambient));
    seed = hash_combine(seed, hash_color(light.directional_color));
    seed = hash_combine(seed, hash_value(light.diffuse));
    seed = hash_combine(seed, hash_shadow_settings(light.shadows));
    seed = hash_combine(seed, hash_value(light.point_lights.size()));
    for (const auto& source : light.point_lights)
        seed = hash_combine(seed, hash_point_light(source));
    return seed;
}

[[nodiscard]] inline u64 hash_material_2_5d(const Material2_5D& material) {
    u64 seed = hash_value(material.accepts_lights);
    seed = hash_combine(seed, hash_value(material.casts_shadows));
    seed = hash_combine(seed, hash_value(material.accepts_shadows));
    seed = hash_combine(seed, hash_value(material.diffuse));
    seed = hash_combine(seed, hash_value(material.ambient_multiplier));
    seed = hash_combine(seed, hash_value(material.specular));
    seed = hash_combine(seed, hash_value(material.shininess));
    return seed;
}

} // namespace chronon3d::rendering

