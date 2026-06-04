#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/shadow_settings.hpp>
#include <chronon3d/scene/model/material_2_5d.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::rendering {

[[nodiscard]] inline Vec3 safe_normalize(Vec3 v, Vec3 fallback = {0.0f, 0.0f, 1.0f}) {
    const f32 len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 1e-8f) {
        return fallback;
    }
    const f32 inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

// Simple directional light for 2.5D rendering.
// Default matches the historical k_box_light / k_light_dir used in FakeBox3D
// so existing visuals are unchanged.
struct LightContext {
    bool enabled{false};
    bool ambient_enabled{false};
    bool directional_enabled{false};
    Vec3 direction{glm::normalize(Vec3(-0.4f, 1.2f, -0.6f))};
    Color ambient_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32  ambient{0.20f};
    Color directional_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32  diffuse{0.80f};
    ShadowSettings shadows{};

    [[nodiscard]] bool empty() const { return !ambient_enabled && !directional_enabled; }

    void enable(bool value = true) { enabled = value; }

    // Lambertian N·L shading. Result in [ambient, ambient+diffuse].
    [[nodiscard]] f32 shade_ndotl(const Vec3& world_normal) const {
        return ambient + diffuse * std::max(0.0f, glm::dot(safe_normalize(world_normal), direction));
    }

    // Same shading gated by material: if accepts_lights=false returns 1 (full lit).
    [[nodiscard]] f32 shade(const Vec3& world_normal, const Material2_5D& mat) const {
        if (!enabled || !mat.accepts_lights) return 1.0f;
        const Vec3 n = safe_normalize(world_normal);
        const Vec3 l = safe_normalize(direction);
        const Vec3 v = {0.0f, 0.0f, 1.0f};
        const Vec3 h = safe_normalize(l + v, v);

        const f32 ndotl = std::max(0.0f, glm::dot(n, l));
        const f32 ndoth = std::max(0.0f, glm::dot(n, h));
        const f32 rim = std::pow(1.0f - std::clamp(glm::dot(n, v), 0.0f, 1.0f), 2.25f);

        const f32 ambient_term = ambient_enabled ? ambient * mat.ambient_multiplier : 0.0f;
        const f32 diffuse_term = directional_enabled
            ? diffuse * mat.diffuse * ndotl
            : 0.0f;
        const f32 specular_term = directional_enabled && mat.specular > 0.0f
            ? diffuse * mat.specular * std::pow(ndoth, std::max(1.0f, mat.shininess))
            : 0.0f;
        const f32 rim_term = directional_enabled ? rim * mat.specular * 0.18f : 0.0f;
        return ambient_term + diffuse_term + specular_term + rim_term;
    }

    // Default scene light: upper-left-front, matching legacy renderer constants.
    static LightContext default_scene() {
        LightContext out;
        out.enabled = true;
        out.ambient_enabled = true;
        out.directional_enabled = true;
        return out;
    }
};

} // namespace chronon3d::rendering
