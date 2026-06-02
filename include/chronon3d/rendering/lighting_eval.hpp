#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
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

[[nodiscard]] inline u64 hash_shadow_settings(const ShadowSettings& s) {
    u64 seed = hash_value(s.opacity);
    seed = hash_combine(seed, hash_value(s.blur_radius));
    seed = hash_combine(seed, hash_value(s.px_per_unit));
    seed = hash_combine(seed, hash_value(s.max_offset));
    seed = hash_combine(seed, hash_value(s.contact_opacity));
    seed = hash_combine(seed, hash_value(s.contact_blur_radius));
    seed = hash_combine(seed, hash_value(s.ambient_opacity));
    seed = hash_combine(seed, hash_value(s.ambient_blur_radius));
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
