#pragma once

#include <chronon3d/evaluation/evaluated_scene.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d {

// Converts an EvaluatedScene into the legacy Scene format so it can be
// rendered by the existing SoftwareRenderer without modification.
//
// Pipeline:
//   SceneDescription
//     -> TimelineEvaluator
//     -> EvaluatedScene
//     -> LegacySceneAdapter
//     -> Scene  (existing renderer input)
//
// The legacy Scene / Layer / RenderNode are not modified by this adapter.
class LegacySceneAdapter {
public:
    [[nodiscard]] Scene convert(const EvaluatedScene& evaluated,
                                std::pmr::memory_resource* res = std::pmr::get_default_resource()) const;
};

} // namespace chronon3d
