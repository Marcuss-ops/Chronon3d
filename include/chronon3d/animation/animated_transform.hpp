#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

struct AnimatedTransform {
    AnimatedValue<Vec3> position{Vec3(0.0f)};
    AnimatedValue<Vec3> rotation_euler{Vec3(0.0f)};  // degrees XYZ, converted to Quat in evaluate()
    AnimatedValue<Vec3> scale{Vec3(1.0f)};
    AnimatedValue<Vec3> anchor{Vec3(0.0f)};
    AnimatedValue<f32>  opacity{1.0f};
    AnimatedValue<f32>  blur{0.0f};  // gaussian blur radius in pixels

    [[nodiscard]] Transform evaluate(Frame frame) const {
        Transform t;
        t.position = position.evaluate(frame);
        t.rotation = glm::quat(glm::radians(rotation_euler.evaluate(frame)));
        t.scale    = scale.evaluate(frame);
        t.anchor   = anchor.evaluate(frame);
        t.opacity  = opacity.evaluate(frame);
        return t;
    }

    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || rotation_euler.is_animated() ||
               scale.is_animated()    || anchor.is_animated() ||
               opacity.is_animated() || blur.is_animated();
    }

    /// Shift every keyframe track by offset frames.
    void shift(Frame offset) {
        position.shift(offset);
        rotation_euler.shift(offset);
        scale.shift(offset);
        anchor.shift(offset);
        opacity.shift(offset);
        blur.shift(offset);
    }
};

} // namespace chronon3d
