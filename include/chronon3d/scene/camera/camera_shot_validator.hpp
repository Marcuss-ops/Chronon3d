#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/transform/transform_resolver.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace chronon3d {

struct CameraLayerShotReport {
    std::string name;
    ProjectedBounds bounds;
    float projected_area{0.0f};
    float center_error_px{0.0f};
    float depth{0.0f};
    bool passed{true};
    std::vector<std::string> failures;
};

struct CameraShotReport {
    std::string composition_name{"Unknown"};
    int frame{0};

    bool passed{true};
    float target_center_error_px{0.0f};

    std::vector<CameraLayerShotReport> layers;
    std::vector<std::string> failures;
};

class CameraShotValidator {
public:
    CameraShotValidator() = default;

    CameraShotValidator& register_layer_size(std::string name, Vec2 size);

    CameraShotValidator& require_target_centered(
        std::string target_name,
        float max_error_px
    );

    CameraShotValidator& require_visible(
        std::string layer_name,
        float min_visible_ratio
    );

    CameraShotValidator& require_inside_safe_area(
        std::string layer_name,
        float safe_margin_norm
    );

    CameraShotValidator& require_depth_order(
        std::vector<std::string> front_to_back
    );

    CameraShotReport validate(
        const Camera2_5D& camera,
        const TransformResolverResult& transforms,
        Viewport viewport
    ) const;

private:
    struct RuleTargetCentered {
        std::string name;
        float threshold_px;
    };
    struct RuleVisible {
        std::string name;
        float min_visible_ratio;
    };
    struct RuleInsideSafeArea {
        std::string name;
        float safe_margin;
    };
    struct RuleDepthOrder {
        std::vector<std::string> ordered_names;
    };

    std::unordered_map<std::string, Vec2> m_layer_sizes;
    std::vector<RuleTargetCentered> m_rules_centered;
    std::vector<RuleVisible> m_rules_visible;
    std::vector<RuleInsideSafeArea> m_rules_safe_area;
    std::vector<RuleDepthOrder> m_rules_depth_order;
};

} // namespace chronon3d
