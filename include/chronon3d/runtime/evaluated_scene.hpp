#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/runtime/evaluated_layer.hpp>
#include <optional>
#include <vector>

namespace chronon3d {

struct EvaluatedScene {
    Frame frame{0};
    i32   width{1280};
    i32   height{720};

    std::vector<EvaluatedLayer> layers;

    std::optional<Camera2_5D> camera;
};

} // namespace chronon3d
