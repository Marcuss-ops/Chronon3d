#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms replaces the legacy TransformResolverResult.
#include <string>
#include <vector>
#include <unordered_map>

namespace chronon3d {

struct CameraFramingOptions {
    float safe_margin_norm{0.08f};
    float min_visible_ratio{0.95f};
    int max_iterations{12};
    float dolly_step{40.0f};
    std::unordered_map<std::string, Vec2> layer_sizes;
};

Camera2_5D fit_camera_to_layers(
    Camera2_5D camera,
    const std::vector<std::string>& layer_names,
    const ResolvedSceneTransforms& transforms,
    Viewport viewport,
    CameraFramingOptions options = {}
);

} // namespace chronon3d
