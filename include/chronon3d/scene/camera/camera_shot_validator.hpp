#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/layer/resolved_types.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <string>
#include <vector>

namespace chronon3d {

struct CameraShotLayerReport {
    std::string name;
    Rect projected_bbox;
    float visible_ratio{1.0f};
    bool inside_canvas{true};
    bool inside_safe_area{true};
    float projected_area{0.0f};
    float safe_area_ratio{1.0f};
};

struct CameraShotReport {
    bool passed{true};
    float target_center_error_px{0.0f};
    std::vector<CameraShotLayerReport> layers;
    std::vector<std::string> failures;
};

struct CameraPathPoint {
    int frame;
    Vec3 position;
    float target_error_px;
};

struct CameraPathMetrics {
    float path_smoothness{0.0f};
    float velocity_continuity{0.0f};
    float acceleration_jump{0.0f};
    std::vector<float> target_center_error_over_time;
};

std::string export_path_debug_json(
    const std::vector<CameraPathPoint>& path,
    const CameraPathMetrics& metrics
);

CameraPathMetrics compute_path_metrics(const std::vector<CameraPathPoint>& path);


class CameraShotValidator {
public:
    explicit CameraShotValidator(Vec2 viewport_size);

    void require_target_centered(std::string name, float threshold_px = 2.0f);
    void require_visible(std::string name, float min_visible_ratio = 0.95f);
    void require_inside_safe_area(std::string name, float min_inside_safe_ratio = 0.90f);
    void require_projected_area_order(std::vector<std::string> ordered_layer_names);

    CameraShotReport validate(
        const Camera2_5D& camera,
        const std::pmr::vector<ResolvedLayer>& resolved_layers,
        const Vec3& target_world_pos
    );

private:
    Vec2 m_viewport;
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
        float min_inside_safe_ratio;
    };
    struct RuleAreaOrder {
        std::vector<std::string> ordered_names;
    };

    std::vector<RuleTargetCentered> m_rules_centered;
    std::vector<RuleVisible> m_rules_visible;
    std::vector<RuleInsideSafeArea> m_rules_safe_area;
    std::vector<RuleAreaOrder> m_rules_area_order;
};

} // namespace chronon3d
