#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/effect_stack.hpp>

namespace chronon3d::presets::motion {

struct MotionEffectState {
    bool glow_enabled{false};
    GlowParams glow{};
    bool shadow_enabled{false};
    DropShadowParams shadow{};
    bool bloom_enabled{false};
    BloomParams bloom{};
};

struct MotionState {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    f32 opacity{1.0f};
    f32 blur{0.0f};
    f32 text_reveal{1.0f};
    f32 mask_reveal{1.0f};
    bool visible{true};

    MotionEffectState effects{};
};

} // namespace chronon3d::presets::motion

