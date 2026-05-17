#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/rendering/light_context.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace chronon3d::rendering {

using namespace chronon3d::graph;

[[nodiscard]] inline Vec3 safe_normalize(Vec3 v, Vec3 fallback = {0.0f, 0.0f, 1.0f}) {
    const f32 len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 1e-8f) {
        return fallback;
    }
    const f32 inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

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
    const Material2_5D& material
) {
    if (!light.enabled || !material.accepts_lights) {
        return base;
    }

    normal_world = safe_normalize(normal_world);
    const f32 ndotl = std::max(0.0f, glm::dot(normal_world, light.direction));

    const f32 ambient = light.ambient_enabled
        ? std::max(0.0f, light.ambient * material.ambient_multiplier)
        : 0.0f;
    const f32 diffuse = light.directional_enabled
        ? std::max(0.0f, light.diffuse * material.diffuse * ndotl)
        : 0.0f;

    const f32 r = std::clamp(ambient * light.ambient_color.r +
                             diffuse * light.directional_color.r, 0.0f, 1.0f);
    const f32 g = std::clamp(ambient * light.ambient_color.g +
                             diffuse * light.directional_color.g, 0.0f, 1.0f);
    const f32 b = std::clamp(ambient * light.ambient_color.b +
                             diffuse * light.directional_color.b, 0.0f, 1.0f);

    return Color{base.r * r, base.g * g, base.b * b, base.a};
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
