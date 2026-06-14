#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

struct AnimatedTransform {
    AnimatedValue<Vec3> position{Vec3(0.0f)};
    AnimatedValue<Vec3> rotation_euler{Vec3(0.0f)};  // degrees XYZ, converted to Quat in evaluate()
    AnimatedValue<Vec3> scale{Vec3(1.0f)};
    AnimatedValue<Vec3> anchor{Vec3(0.0f)};
    AnimatedValue<f32>  opacity{1.0f};
    AnimatedValue<f32>  blur{0.0f};  // gaussian blur radius in pixels

    /// Sub-frame evaluation — the primary entry point for continuous-time animation.
    [[nodiscard]] Transform evaluate(SampleTime time) const {
        Transform t;
        t.position = position.evaluate(time);
        t.rotation = glm::quat(glm::radians(rotation_euler.evaluate(time)));
        t.scale    = scale.evaluate(time);
        t.anchor   = anchor.evaluate(time);
        t.opacity  = opacity.evaluate(time);
        return t;
    }

    /// Legacy integer-frame evaluation (backward compatible).
    [[nodiscard]] Transform evaluate(Frame frame) const {
        return evaluate(SampleTime::from_frame_int(frame));
    }

    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || rotation_euler.is_animated() ||
               scale.is_animated()    || anchor.is_animated() ||
               opacity.is_animated() || blur.is_animated();
    }

    /// Returns true if any component depends on time — keyframes OR expressions.
    /// Use this (not is_animated()) when deciding whether the transform must be
    /// re-evaluated every frame/sub-frame.
    [[nodiscard]] bool is_time_dependent() const {
        return position.is_time_dependent() || rotation_euler.is_time_dependent() ||
               scale.is_time_dependent()    || anchor.is_time_dependent() ||
               opacity.is_time_dependent() || blur.is_time_dependent();
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
