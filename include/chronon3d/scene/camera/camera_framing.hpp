#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/transform/transform_resolver.hpp>
#include <string>
#include <vector>

namespace chronon3d {

struct CameraFramingOptions {
    float safe_margin_norm{0.08f};
    float min_visible_ratio{0.95f};
    int max_iterations{12};
    float dolly_step{40.0f};
};

Camera2_5D fit_camera_to_layers(
    Camera2_5D camera,
    const std::vector<std::string>& layer_names,
    const TransformResolverResult& transforms,
    Viewport viewport,
    CameraFramingOptions options = {}
);

} // namespace chronon3d
