#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/animated_value.hpp>

namespace chronon3d {

struct AnimatedTransform {
    AnimatedValue<Vec3> position{Vec3(0.0f)};
    AnimatedValue<Quat> rotation{Quat::identity()};
    AnimatedValue<Vec3> scale{Vec3(1.0f)};

    [[nodiscard]] Transform evaluate(Frame frame) const {
        return {
            position.evaluate(frame),
            rotation.evaluate(frame),
            scale.evaluate(frame)
        };
    }

    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || rotation.is_animated() || scale.is_animated();
    }
};

} // namespace chronon3d
