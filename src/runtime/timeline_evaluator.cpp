#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/expression.hpp>
#include <variant>
#include <unordered_map>

namespace chronon3d {

namespace {

inline f32 resolve_z(const LayerDesc& l, Vec3 evaluated_pos) {
    if (l.is_3d && l.depth_role != DepthRole::None) {
        return DepthRoleResolver::z_for(l.depth_role) + l.depth_offset;
    }
    return evaluated_pos.z;
}

inline EffectStack resolve_effects(const std::vector<EffectDesc>& descs) {
    EffectStack stack;
    for (const auto& d : descs) {
        std::visit([&stack](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlurEffectDesc>) {
                stack.push_back(EffectInstance{BlurParams{e.radius}});
            } else if constexpr (std::is_same_v<T, TintEffectDesc>) {
                stack.push_back(EffectInstance{TintParams{e.color, 1.0f}});
            } else if constexpr (std::is_same_v<T, BrightnessContrastEffectDesc>) {
                if (e.brightness != 0.0f)
                    stack.push_back(EffectInstance{BrightnessParams{e.brightness}});
                if (e.contrast != 1.0f)
                    stack.push_back(EffectInstance{ContrastParams{e.contrast}});
            }
        }, d);
    }
    return stack;
}

inline f32 eval_expr(const std::string& expr, double frame, double time, double w, double h, f32 fallback) {
    const std::unordered_map<std::string, double> vars{
        {"frame", frame}, {"time", time}, {"width", w}, {"height", h},
    };
    return static_cast<f32>(math::evaluate_expression(expr, vars, fallback));
}

} // namespace

EvaluatedScene TimelineEvaluator::evaluate(const SceneDescription& scene, Frame frame) const {
    EvaluatedScene result;
    result.frame  = frame;
    result.width  = scene.width;
    result.height = scene.height;

    for (const auto& ld : scene.layers) {
        if (!ld.time_range.contains(frame)) continue;

        EvaluatedLayer el;
        el.name       = ld.name;
        el.visible    = true;

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);

        if (ld.opacity.has_expression()) {
            el.opacity = eval_expr(ld.opacity.expression(), (double)frame, time, (double)scene.width, (double)scene.height, ld.opacity.value_at(frame));
        } else {
            el.opacity = ld.opacity.value_at(frame);
        }

        el.is_3d      = ld.is_3d;
        el.depth_role = ld.depth_role;
        el.blend_mode = ld.blend_mode;

        Vec3 pos = ld.position.value_at(frame);
        Vec3 rot = ld.rotation.value_at(frame);
        Vec3 scl = ld.scale.value_at(frame);

        el.resolved_z      = resolve_z(ld, pos);
        pos.z              = el.resolved_z;

        el.world_transform.position = pos;
        el.world_transform.rotation = math::camera_rotation_quat(rot);
        el.world_transform.scale    = scl;
        el.world_transform.opacity  = el.opacity;

        el.visuals = ld.visuals;
        el.resolved_effects = resolve_effects(ld.effects);

        result.layers.push_back(std::move(el));
    }

    if (scene.camera && scene.camera->enabled) {
        Camera2_5D cam;
        cam.enabled            = true;
        cam.position           = scene.camera->position.value_at(frame);
        cam.point_of_interest  = scene.camera->point_of_interest;
        cam.point_of_interest_enabled = scene.camera->point_of_interest_enabled;
        cam.rotation           = scene.camera->rotation.value_at(frame);

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);
        if (scene.camera->zoom.has_expression()) {
            cam.zoom = eval_expr(scene.camera->zoom.expression(), (double)frame, time, (double)scene.width, (double)scene.height, scene.camera->zoom.value_at(frame));
        } else {
            cam.zoom = scene.camera->zoom.value_at(frame);
        }
        result.camera = cam;
    }

    return result;
}

} // namespace chronon3d
