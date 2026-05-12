#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/evaluation/evaluated_layer.hpp>
#include <optional>
#include <vector>

namespace chronon3d {

// A scene with all animated values resolved at a specific frame.
// This is the output of TimelineEvaluator and the input of LegacySceneAdapter.
struct EvaluatedScene {
    Frame frame{0};
    i32   width{1280};
    i32   height{720};

    std::vector<EvaluatedLayer> layers;

    // Camera resolved from Camera2_5DDesc (if present and enabled).
    std::optional<Camera2_5D> camera;
};

} // namespace chronon3d
