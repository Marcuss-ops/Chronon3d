#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>

#include <string>
#include <vector>

namespace chronon3d {

// Renderer-facing view model used by the optional camera debug overlay. Camera
// geometry validation itself is owned by ChrononMotion3D::CameraValidator.
struct CameraLayerShotReport {
    std::string name;
    ProjectedBounds bounds;
    float projected_area{0.0f};
    float center_error_px{0.0f};
    float depth{0.0f};
    bool passed{true};
    std::vector<std::string> failures;
};

struct VisibilityMetrics {
    float visible_pixel_ratio{1.0f};
    float max_layer_visibility{0.0f};
    float estimated_visible_area{0.0f};
    bool is_black_frame{false};
};

struct CameraShotReport {
    std::string composition_name{"Unknown"};
    int frame{0};
    bool passed{true};
    float target_center_error_px{0.0f};
    VisibilityMetrics visibility;
    std::vector<CameraLayerShotReport> layers;
    std::vector<std::string> failures;
};

} // namespace chronon3d
