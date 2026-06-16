#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
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

/// Global frame visibility metrics — estimated from layer bounding boxes.
/// A frame that is entirely black (is_black_frame = true) indicates
/// a camera or scene setup error and must fail validation.
struct VisibilityMetrics {
    float visible_pixel_ratio{1.0f};    // estimated bbox-area / viewport-area
    float max_layer_visibility{0.0f};   // max visible_ratio across all layers
    float estimated_visible_area{0.0f}; // Σ(bbox_area * visible_ratio)
    bool is_black_frame{false};         // true if estimated_visible_area == 0
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

class CameraShotValidator {
public:
    CameraShotValidator() = default;

    CameraShotValidator& register_layer_size(std::string name, Vec2 size);

    [[nodiscard]] const std::unordered_map<std::string, Vec2>& layer_sizes() const { return m_layer_sizes; }

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

    /// Mandatory global visibility check — fails frames that are
    /// entirely black or have too few visible pixels.
    /// Default thresholds: 0 pixels = black frame, < 0.5% visible = too dark.
    const CameraShotValidator& require_frame_visibility(
        float min_visible_ratio = 0.005f
    ) const;

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

    // Global visibility thresholds (default: disabled).
    // Mutable because require_frame_visibility() is called on const validators
    // from the camera_test_orchestrator's const CameraShotProfile reference.
    mutable bool  m_frame_visibility_enabled{false};
    mutable float m_min_frame_visible_ratio{0.005f};
};

} // namespace chronon3d
