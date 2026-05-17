#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/scene/material_2_5d.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::rendering {

// Simple directional light for 2.5D rendering.
// Default matches the historical k_box_light / k_light_dir used in FakeBox3D
// and FakeExtrudedText, so existing visuals are unchanged.
struct LightContext {
    bool enabled{false};
    bool ambient_enabled{false};
    bool directional_enabled{false};
    Vec3 direction{glm::normalize(Vec3(-0.4f, 1.2f, -0.6f))};
    Color ambient_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32  ambient{0.20f};
    Color directional_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32  diffuse{0.80f};

    [[nodiscard]] bool empty() const { return !ambient_enabled && !directional_enabled; }

    void enable(bool value = true) { enabled = value; }

    // Lambertian N·L shading. Result in [ambient, ambient+diffuse].
    [[nodiscard]] f32 shade_ndotl(const Vec3& world_normal) const {
        return ambient + diffuse * std::max(0.0f, glm::dot(world_normal, direction));
    }

    // Same shading gated by material: if accepts_lights=false returns 1 (full lit).
    [[nodiscard]] f32 shade(const Vec3& world_normal, const Material2_5D& mat) const {
        if (!enabled || !mat.accepts_lights) return 1.0f;
        const f32 ambient_term = ambient_enabled ? ambient * mat.ambient_multiplier : 0.0f;
        const f32 diffuse_term = directional_enabled
            ? diffuse * mat.diffuse * std::max(0.0f, glm::dot(world_normal, direction))
            : 0.0f;
        return ambient_term + diffuse_term;
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
