#pragma once

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
    Vec3 direction{glm::normalize(Vec3(-0.4f, 1.2f, -0.6f))};
    f32  ambient{0.20f};
    f32  diffuse{0.80f};

    // Lambertian N·L shading. Result in [ambient, ambient+diffuse].
    [[nodiscard]] f32 shade_ndotl(const Vec3& world_normal) const {
        return ambient + diffuse * std::max(0.0f, glm::dot(world_normal, direction));
    }

    // Same shading gated by material: if accepts_lights=false returns 1 (full lit).
    [[nodiscard]] f32 shade(const Vec3& world_normal, const Material2_5D& mat) const {
        if (!mat.accepts_lights) return 1.0f;
        const f32 base = shade_ndotl(world_normal);
        return ambient * mat.ambient_multiplier + diffuse * mat.diffuse *
               std::max(0.0f, glm::dot(world_normal, direction));
    }

    // Default scene light: upper-left-front, matching legacy renderer constants.
    static LightContext default_scene() {
        return LightContext{};
    }
};

} // namespace chronon3d::rendering
