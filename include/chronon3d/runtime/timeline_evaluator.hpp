#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <memory_resource>

namespace chronon3d {

class TimelineEvaluator {
public:
    [[nodiscard]] Scene evaluate(const SceneDescription& scene, Frame frame,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const;
};

} // namespace chronon3d
