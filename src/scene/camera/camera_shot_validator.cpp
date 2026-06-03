#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d {

CameraShotValidator& CameraShotValidator::register_layer_size(std::string name, Vec2 size) {
    m_layer_sizes[name] = size;
    return *this;
}

CameraShotValidator& CameraShotValidator::require_target_centered(std::string target_name, float max_error_px) {
    m_rules_centered.push_back({std::move(target_name), max_error_px});
    return *this;
}

CameraShotValidator& CameraShotValidator::require_visible(std::string layer_name, float min_visible_ratio) {
    m_rules_visible.push_back({std::move(layer_name), min_visible_ratio});
    return *this;
}

CameraShotValidator& CameraShotValidator::require_inside_safe_area(std::string layer_name, float safe_margin_norm) {
    m_rules_safe_area.push_back({std::move(layer_name), safe_margin_norm});
    return *this;
}

CameraShotValidator& CameraShotValidator::require_depth_order(std::vector<std::string> front_to_back) {
    m_rules_depth_order.push_back({std::move(front_to_back)});
    return *this;
}

CameraShotReport CameraShotValidator::validate(
    const Camera2_5D& camera,
    const TransformResolverResult& transforms,
    Viewport viewport
) const {
    CameraShotReport report;
    report.passed = true;

    std::unordered_map<std::string, CameraLayerShotReport> layer_reports;

    auto get_or_create_layer_report = [&](const std::string& name) -> CameraLayerShotReport& {
        auto it = layer_reports.find(name);
        if (it != layer_reports.end()) {
            return it->second;
        }
        CameraLayerShotReport lr;
        lr.name = name;
        lr.passed = true;

        auto pos_opt = transforms.world_position(name);
        auto mat_opt = transforms.world_matrix(name);

        if (!mat_opt) {
            lr.passed = false;
            lr.failures.push_back("Layer transform not found in hierarchy resolution.");
        } else {
            Vec2 size{200.0f, 200.0f};
            auto size_it = m_layer_sizes.find(name);
            if (size_it != m_layer_sizes.end()) {
                size = size_it->second;
            }
            
            const Mat4& world_m = *mat_opt;
            std::array<Vec3, 4> corners = {
                Vec3(world_m * Vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(size.x, 0.0f, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(size.x, size.y, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(0.0f, size.y, 0.0f, 1.0f))
            };

            lr.bounds = project_quad_to_screen(corners, camera, viewport);
            lr.projected_area = (lr.bounds.max.x - lr.bounds.min.x) * (lr.bounds.max.y - lr.bounds.min.y);

            Vec2 local_center{size.x * 0.5f, size.y * 0.5f};
            Vec3 world_center = Vec3(world_m * Vec4(local_center, 0.0f, 1.0f));
            ScreenPoint sp = project_world_to_screen(world_center, camera, viewport);
            lr.depth = sp.depth;
            if (sp.behind_camera) {
                lr.center_error_px = 999999.0f;
            } else {
                lr.center_error_px = glm::distance(sp.position, Vec2{viewport.width * 0.5f, viewport.height * 0.5f});
            }
        }
        return layer_reports.emplace(name, std::move(lr)).first->second;
    };

    for (const auto& rule : m_rules_centered) {
        auto pos_opt = transforms.world_position(rule.name);
        if (!pos_opt) {
            report.passed = false;
            report.failures.push_back("Target '" + rule.name + "' not resolved in hierarchy.");
            continue;
        }
        ScreenPoint sp = project_world_to_screen(*pos_opt, camera, viewport);
        float err = 999999.0f;
        if (!sp.behind_camera) {
            err = glm::distance(sp.position, Vec2{viewport.width * 0.5f, viewport.height * 0.5f});
        }
        report.target_center_error_px = std::max(report.target_center_error_px, err);

        if (err > rule.threshold_px) {
            report.passed = false;
            report.failures.push_back("Target '" + rule.name + "' deviation " + std::to_string(err) + "px exceeds threshold " + std::to_string(rule.threshold_px) + "px.");
        }
    }

    for (const auto& rule : m_rules_visible) {
        auto& lr = get_or_create_layer_report(rule.name);
        if (!lr.passed) {
            report.passed = false;
            continue;
        }
        if (lr.bounds.visible_ratio < rule.min_visible_ratio) {
            lr.passed = false;
            report.passed = false;
            std::string fail_msg = "Layer '" + rule.name + "' visible ratio " + std::to_string(lr.bounds.visible_ratio) + " is below minimum " + std::to_string(rule.min_visible_ratio);
            lr.failures.push_back(fail_msg);
            report.failures.push_back(fail_msg);
        }
    }

    for (const auto& rule : m_rules_safe_area) {
        auto& lr = get_or_create_layer_report(rule.name);
        if (!lr.passed) {
            report.passed = false;
            continue;
        }

        float sx1 = viewport.width * rule.safe_margin;
        float sy1 = viewport.height * rule.safe_margin;
        float sx2 = viewport.width * (1.0f - rule.safe_margin);
        float sy2 = viewport.height * (1.0f - rule.safe_margin);

        bool inside_custom_safe = (lr.bounds.min.x >= sx1 && lr.bounds.max.x <= sx2 &&
                                   lr.bounds.min.y >= sy1 && lr.bounds.max.y <= sy2);

        if (!inside_custom_safe) {
            lr.passed = false;
            report.passed = false;
            std::string fail_msg = "Layer '" + rule.name + "' is outside safe area margin " + std::to_string(rule.safe_margin);
            lr.failures.push_back(fail_msg);
            report.failures.push_back(fail_msg);
        }
    }

    for (const auto& rule : m_rules_depth_order) {
        for (size_t i = 0; i + 1 < rule.ordered_names.size(); ++i) {
            auto& lr1 = get_or_create_layer_report(rule.ordered_names[i]);
            auto& lr2 = get_or_create_layer_report(rule.ordered_names[i+1]);
            if (!lr1.passed || !lr2.passed) {
                report.passed = false;
                continue;
            }
            if (lr1.depth >= lr2.depth) {
                lr1.passed = false;
                report.passed = false;
                std::string fail_msg = "Depth order violation: " + rule.ordered_names[i] + " depth (" + std::to_string(lr1.depth) + ") is not less than " + rule.ordered_names[i+1] + " depth (" + std::to_string(lr2.depth) + ").";
                lr1.failures.push_back(fail_msg);
                report.failures.push_back(fail_msg);
            }
        }
    }

    for (auto&& pair : layer_reports) {
        report.layers.push_back(std::move(pair.second));
    }

    return report;
}

} // namespace chronon3d
