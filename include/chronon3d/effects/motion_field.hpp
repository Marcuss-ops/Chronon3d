#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::effects {

enum class MotionFieldMode : std::uint8_t {
    LayerAffine,  // Constant velocity across entire layer (derived from transform delta)
    Radial,       // Radial expansion/contraction from layer center
    Directional   // Fixed directional vector field
};

/// Authority representing motion velocity for temporal RGB split, motion blur, and trails.
struct MotionField {
    MotionFieldMode mode{MotionFieldMode::LayerAffine};
    Vec2 velocity{0.0f, 0.0f};  // Screen-space velocity in pixels per frame
    Vec2 center{0.5f, 0.5f};    // Normalized layer center
    float scale{1.0f};          // Multiplier

    [[nodiscard]] Vec2 sample(Vec2 uv) const noexcept {
        if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
            return Vec2{0.0f, 0.0f};

        switch (mode) {
            case MotionFieldMode::LayerAffine:
            case MotionFieldMode::Directional:
                return velocity * scale;
            case MotionFieldMode::Radial: {
                const Vec2 diff = uv - center;
                const float dist = glm::length(diff);
                if (dist < 1.0e-5f) return Vec2{0.0f, 0.0f};
                return (diff / dist) * (glm::length(velocity) * scale * dist);
            }
        }
        return Vec2{0.0f, 0.0f};
    }
};

} // namespace chronon3d::effects
