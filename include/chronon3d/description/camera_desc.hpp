#pragma once

#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/core/types.hpp>

namespace chronon3d {

// Descriptive (pre-evaluation) camera for SceneDescription.
// position is AnimatedValue<Vec3> so camera moves can be keyframed.
struct Camera2_5DDesc {
    bool                enabled{false};
    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, -1000.0f}};
    Vec3                point_of_interest{0.0f, 0.0f, 0.0f};
    AnimatedValue<f32>  zoom{1000.0f};
};

} // namespace chronon3d
