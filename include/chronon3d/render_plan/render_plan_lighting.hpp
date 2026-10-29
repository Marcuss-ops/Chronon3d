#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>

#include <array>
#include <optional>
#include <string>

namespace chronon3d::render_plan {

struct LightingRigPlan {
    std::array<float, 3> key_direction{-0.2857143f, 0.8571429f, -0.4285714f};
    std::array<float, 4> key_color{1.0f, 1.0f, 1.0f, 1.0f};
    float key_intensity{0.80f};
    std::array<float, 4> ambient_color{0.6f, 0.65f, 0.7f, 1.0f};
    float ambient_intensity{0.25f};
    bool rim_enabled{false};
    std::array<float, 4> rim_color{1.0f, 1.0f, 1.0f, 1.0f};
    float rim_intensity{0.30f};
    float rim_power{2.2f};
};

/// Bounded RenderPlan reference to a caller-provided HDRI_ENV_V1 asset. The
/// asset contains cubemap faces/mips; SH is duplicated here for deterministic
/// diffuse-probe access without decoding the image during every frame.
struct EnvironmentLightPlan {
    std::string probe_asset{};
    float intensity{1.0f};
    float rotation_y{0.0f};
    std::array<float, 27> irradiance_sh{};
    std::optional<AnimationTrackPlan> rotation_animation{};
};


} // namespace chronon3d::render_plan
