#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/evaluation/evaluated_scene.hpp>

namespace chronon3d {

// Evaluates a SceneDescription at a given frame, producing an EvaluatedScene.
// All AnimatedValue<T> are resolved; time ranges are checked; depth roles
// are converted to world-space Z.
//
// This stage is pure computation: no rendering, no I/O.
class TimelineEvaluator {
public:
    [[nodiscard]] EvaluatedScene evaluate(const SceneDescription& scene, Frame frame) const;
};

} // namespace chronon3d
