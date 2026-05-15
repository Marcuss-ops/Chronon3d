#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/evaluated_scene.hpp>

namespace chronon3d {

class TimelineEvaluator {
public:
    [[nodiscard]] EvaluatedScene evaluate(const SceneDescription& scene, Frame frame) const;
};

} // namespace chronon3d
