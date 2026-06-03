#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d {

CameraShotValidator::CameraShotValidator(Vec2 viewport_size)
    : m_viewport(viewport_size) {}

void CameraShotValidator::require_target_centered(std::string name, float threshold_px) {
    m_rules_centered.push_back({std::move(name), threshold_px});
}

void CameraShotValidator::require_visible(std::string name, float min_visible_ratio) {
    m_rules_visible.push_back({std::move(name), min_visible_ratio});
}

void CameraShotValidator::require_inside_safe_area(std::string name, float min_inside_safe_ratio) {
    m_rules_safe_area.push_back({std::move(name), min_inside_safe_ratio});
}

void CameraShotValidator::require_projected_area_order(std::vector<std::string> ordered_layer_names) {
    m_rules_area_order.push_back({std::move(ordered_layer_names)});
}

static inline float compute_intersection_area(const Rect& r1, const Rect& r2) {
    float x1 = std::max(r1.origin.x, r2.origin.x);
    float y1 = std::max(r1.origin.y, r2.origin.y);
    float x2 = std::min(r1.origin.x + r1.size.x, r2.origin.x + r2.size.x);
    float y2 = std::min(r1.origin.y + r1.size.y, r2.origin.y + r2.size.y);
    if (x2 > x1 && y2 > y1) {
        return (x2 - x1) * (y2 - y1);
    }
    return 0.0f;
}

static inline std::array<Vec3, 4> get_layer_world_corners(const ResolvedLayer& rl) {
    float w = 200.0f;
    float h = 200.0f;
    if (rl.layer && !rl.layer->nodes.empty()) {
        const auto& node = rl.layer->nodes.front();
        if (node.shape.type == ShapeType::Rect) {
            w = node.shape.rect.size.x;
            h = node.shape.rect.size.y;
        } else if (node.shape.type == ShapeType::RoundedRect) {
            w = node.shape.rounded_rect.size.x;
            h = node.shape.rounded_rect.size.y;
        } else if (node.shape.type == ShapeType::Image) {
            w = node.shape.image.size.x;
            h = node.shape.image.size.y;
        } else if (node.shape.type == ShapeType::FakeBox3D) {
            w = node.shape.fake_box3d.size.x;
            h = node.shape.fake_box3d.size.y;
        }
    }

    std::array<Vec3, 4> local_corners = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{w, 0.0f, 0.0f},
        Vec3{w, h, 0.0f},
        Vec3{0.0f, h, 0.0f}
    };

    std::array<Vec3, 4> world_corners;
    for (size_t i = 0; i < 4; ++i) {
        world_corners[i] = Vec3(rl.world_matrix * Vec4(local_corners[i], 1.0f));
    }
    return world_corners;
}

CameraShotReport CameraShotValidator::validate(
    const Camera2_5D& camera,
    const std::pmr::vector<ResolvedLayer>& resolved_layers,
    const Vec3& target_world_pos
) {
    CameraShotReport report;
    report.passed = true;

    Rect canvas_rect{ {0.0f, 0.0f}, m_viewport };
    Rect safe_rect{ m_viewport * 0.05f, m_viewport * 0.90f };

    for (const auto& rl : resolved_layers) {
        if (!rl.layer) continue;
        if (!rl.layer->uses_2_5d_projection) continue;

        CameraShotLayerReport layer_report;
        layer_report.name = std::string(rl.layer->name);

        std::array<Vec3, 4> corners = get_layer_world_corners(rl);
        ProjectedBounds pb = project_bounds_to_screen(corners, camera, m_viewport);

        if (pb.fully_clipped) {
            layer_report.projected_bbox = Rect{ {0.0f, 0.0f}, {0.0f, 0.0f} };
            layer_report.visible_ratio = 0.0f;
            layer_report.inside_canvas = false;
            layer_report.inside_safe_area = false;
            layer_report.projected_area = 0.0f;
            layer_report.safe_area_ratio = 0.0f;
        } else {
            layer_report.projected_bbox = Rect{ pb.min, pb.max - pb.min };
            float total_area = layer_report.projected_bbox.size.x * layer_report.projected_bbox.size.y;
            layer_report.projected_area = total_area;

            if (total_area > 0.0f) {
                float intersect_canvas = compute_intersection_area(layer_report.projected_bbox, canvas_rect);
                layer_report.visible_ratio = intersect_canvas / total_area;

                float intersect_safe = compute_intersection_area(layer_report.projected_bbox, safe_rect);
                layer_report.safe_area_ratio = intersect_safe / total_area;
            } else {
                layer_report.visible_ratio = 0.0f;
                layer_report.safe_area_ratio = 0.0f;
            }

            layer_report.inside_canvas = (layer_report.visible_ratio > 0.0f);
            layer_report.inside_safe_area = (layer_report.safe_area_ratio >= 0.90f);
        }

        report.layers.push_back(layer_report);
    }

    Vec2 target_screen = project_world_to_screen(target_world_pos, camera, m_viewport);
    Vec2 center = m_viewport * 0.5f;
    float err = 0.0f;
    if (target_screen.x <= -999998.0f) {
        err = 999999.0f;
    } else {
        err = glm::distance(target_screen, center);
    }
    report.target_center_error_px = err;

    for (const auto& rule : m_rules_centered) {
        if (err > rule.threshold_px) {
            report.passed = false;
            report.failures.push_back("Target '" + rule.name + "' is not centered. Error: " +
                                      std::to_string(err) + " px (threshold: " +
                                      std::to_string(rule.threshold_px) + ")");
        }
    }

    auto find_report = [&](const std::string& name) -> const CameraShotLayerReport* {
        for (const auto& lr : report.layers) {
            if (lr.name == name) return &lr;
        }
        return nullptr;
    };

    for (const auto& rule : m_rules_visible) {
        const auto* lr = find_report(rule.name);
        if (!lr) {
            report.passed = false;
            report.failures.push_back("Required layer '" + rule.name + "' not found in shot report.");
        } else if (lr->visible_ratio < rule.min_visible_ratio) {
            report.passed = false;
            report.failures.push_back("Layer '" + rule.name + "' visible ratio " +
                                      std::to_string(lr->visible_ratio) + " is below minimum " +
                                      std::to_string(rule.min_visible_ratio));
        }
    }

    for (const auto& rule : m_rules_safe_area) {
        const auto* lr = find_report(rule.name);
        if (!lr) {
            report.passed = false;
            report.failures.push_back("Required layer '" + rule.name + "' not found in shot report.");
        } else if (lr->safe_area_ratio < rule.min_inside_safe_ratio) {
            report.passed = false;
            report.failures.push_back("Layer '" + rule.name + "' safe area ratio " +
                                      std::to_string(lr->safe_area_ratio) + " is below minimum " +
                                      std::to_string(rule.min_inside_safe_ratio));
        }
    }

    for (const auto& rule : m_rules_area_order) {
        for (size_t i = 0; i + 1 < rule.ordered_names.size(); ++i) {
            const auto* lr1 = find_report(rule.ordered_names[i]);
            const auto* lr2 = find_report(rule.ordered_names[i + 1]);
            if (!lr1 || !lr2) {
                report.passed = false;
                report.failures.push_back("Layer ordering refers to missing layer: " +
                                          rule.ordered_names[i] + " or " + rule.ordered_names[i + 1]);
            } else if (lr1->projected_area <= lr2->projected_area) {
                report.passed = false;
                report.failures.push_back("Projected area order violated: " +
                                          rule.ordered_names[i] + " (" + std::to_string(lr1->projected_area) +
                                          ") is not greater than " +
                                          rule.ordered_names[i+1] + " (" + std::to_string(lr2->projected_area) + ")");
            }
        }
    }

    return report;
}

CameraPathMetrics compute_path_metrics(const std::vector<CameraPathPoint>& path) {
    CameraPathMetrics metrics;
    if (path.size() < 2) return metrics;

    std::vector<Vec3> velocities;
    velocities.reserve(path.size() - 1);
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        float dt = static_cast<float>(path[i+1].frame - path[i].frame);
        if (dt <= 0.0f) dt = 1.0f;
        velocities.push_back((path[i+1].position - path[i].position) / dt);
    }

    std::vector<Vec3> accelerations;
    if (velocities.size() >= 2) {
        accelerations.reserve(velocities.size() - 1);
        for (size_t i = 0; i + 1 < velocities.size(); ++i) {
            float dt = static_cast<float>(path[i+2].frame - path[i+1].frame);
            if (dt <= 0.0f) dt = 1.0f;
            accelerations.push_back((velocities[i+1] - velocities[i]) / dt);
        }
    }

    float sum_acc_mag = 0.0f;
    for (const auto& acc : accelerations) {
        sum_acc_mag += glm::length(acc);
    }
    metrics.path_smoothness = accelerations.empty() ? 0.0f : (sum_acc_mag / static_cast<float>(accelerations.size()));

    float sum_vel_diff = 0.0f;
    for (size_t i = 0; i + 1 < velocities.size(); ++i) {
        sum_vel_diff += glm::distance(velocities[i+1], velocities[i]);
    }
    metrics.velocity_continuity = (velocities.size() < 2) ? 0.0f : (sum_vel_diff / static_cast<float>(velocities.size() - 1));

    float max_acc_jump = 0.0f;
    for (size_t i = 0; i + 1 < accelerations.size(); ++i) {
        max_acc_jump = std::max(max_acc_jump, glm::distance(accelerations[i+1], accelerations[i]));
    }
    metrics.acceleration_jump = max_acc_jump;

    metrics.target_center_error_over_time.reserve(path.size());
    for (const auto& pt : path) {
        metrics.target_center_error_over_time.push_back(pt.target_error_px);
    }

    return metrics;
}

std::string export_path_debug_json(
    const std::vector<CameraPathPoint>& path,
    const CameraPathMetrics& metrics
) {
    std::string json = "{\n  \"camera_path\": [\n";
    for (size_t i = 0; i < path.size(); ++i) {
        const auto& pt = path[i];
        json += "    {\"frame\": " + std::to_string(pt.frame) +
                ", \"x\": " + std::to_string(pt.position.x) +
                ", \"y\": " + std::to_string(pt.position.y) +
                ", \"z\": " + std::to_string(pt.position.z) + "}";
        if (i + 1 < path.size()) {
            json += ",\n";
        }
    }
    json += "\n  ],\n";
    json += "  \"metrics\": {\n";
    json += "    \"path_smoothness\": " + std::to_string(metrics.path_smoothness) + ",\n";
    json += "    \"velocity_continuity\": " + std::to_string(metrics.velocity_continuity) + ",\n";
    json += "    \"acceleration_jump\": " + std::to_string(metrics.acceleration_jump) + ",\n";
    json += "    \"target_center_error_over_time\": [";
    for (size_t i = 0; i < metrics.target_center_error_over_time.size(); ++i) {
        json += std::to_string(metrics.target_center_error_over_time[i]);
        if (i + 1 < metrics.target_center_error_over_time.size()) {
            json += ", ";
        }
    }
    json += "]\n  }\n}";
    return json;
}

} // namespace chronon3d
