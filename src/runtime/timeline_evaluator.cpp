#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/expression.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/shape.hpp>
#include <variant>
#include <unordered_map>

namespace chronon3d {

namespace {

RenderNode make_render_node(const VisualDesc& vd,
                            const Transform& layer_transform,
                            std::pmr::memory_resource* res) {
    RenderNode node(res);

    std::visit([&node, &layer_transform, res](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, RectParams>) {
            node.name            = std::pmr::string{"rect", res};
            node.shape.type      = ShapeType::Rect;
            node.shape.rect.size = v.size;
            node.shape.rect.stroke = v.stroke;
            node.color           = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, RoundedRectParams>) {
            node.name                         = std::pmr::string{"rrect", res};
            node.shape.type                   = ShapeType::RoundedRect;
            node.shape.rounded_rect.size      = v.size;
            node.shape.rounded_rect.radius    = v.radius;
            node.shape.rounded_rect.stroke    = v.stroke;
            node.color                        = v.color;
            node.world_transform.position     = layer_transform.position + v.pos;
            node.world_transform.anchor       = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, CircleParams>) {
            node.name                     = std::pmr::string{"circle", res};
            node.shape.type               = ShapeType::Circle;
            node.shape.circle.radius      = v.radius;
            node.shape.circle.stroke      = v.stroke;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.radius, v.radius, 0.0f};

        } else if constexpr (std::is_same_v<T, LineParams>) {
            node.name                     = std::pmr::string{"line", res};
            node.shape.type               = ShapeType::Line;
            node.shape.line.to            = v.to - v.from;
            node.shape.line.thickness     = v.thickness;
            node.shape.line.stroke.enabled = v.stroke.enabled;
            node.shape.line.stroke.color   = v.stroke.color;
            node.shape.line.stroke.width   = v.stroke.width;
            node.shape.line.stroke.alignment = v.stroke.alignment;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.from;
            node.world_transform.anchor   = {0.0f, 0.0f, 0.0f};

        } else if constexpr (std::is_same_v<T, ImageParams>) {
            node.name                     = std::pmr::string{"image", res};
            node.shape.type               = ShapeType::Image;
            node.shape.image.path         = v.path;
            node.shape.image.size         = v.size;
            node.shape.image.opacity      = v.opacity;
            node.color                    = Color{1.0f, 1.0f, 1.0f, v.opacity};
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};
        }

        node.world_transform.scale    = layer_transform.scale;
        node.world_transform.rotation = layer_transform.rotation;
    }, vd);

    return node;
}

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

Scene TimelineEvaluator::evaluate(const SceneDescription& scene, Frame frame, std::pmr::memory_resource* res) const {
    Scene result(res);

    for (const auto& ld : scene.layers) {
        if (!ld.time_range.contains(frame)) continue;

        Layer layer(res);
        layer.name         = std::pmr::string{ld.name, res};
        layer.uses_2_5d_projection = ld.is_3d;
        layer.depth_role   = ld.depth_role;
        layer.blend_mode   = ld.blend_mode;
        layer.effects      = resolve_effects(ld.effects);
        layer.transition_in = ld.transition_in;
        layer.transition_out = ld.transition_out;
        layer.visible      = true;

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);

        f32 opacity = 1.0f;
        if (ld.opacity.has_expression()) {
            opacity = eval_expr(ld.opacity.expression(), (double)frame, time, (double)scene.width, (double)scene.height, ld.opacity.value_at(frame));
        } else {
            opacity = ld.opacity.value_at(frame);
        }

        Vec3 pos = ld.position.value_at(frame);
        Vec3 rot = ld.rotation.value_at(frame);
        Vec3 scl = ld.scale.value_at(frame);

        f32 resolved_z = resolve_z(ld, pos);
        pos.z = resolved_z;

        layer.transform.position = pos;
        layer.transform.rotation = math::camera_rotation_quat(rot);
        layer.transform.scale    = scl;
        layer.transform.opacity  = opacity;

        for (const auto& vd : ld.visuals) {
            layer.nodes.push_back(make_render_node(vd, layer.transform, res));
        }

        result.add_layer(std::move(layer));
    }

    if (scene.camera && scene.camera->enabled) {
        Camera2_5DRuntime cam;
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

        auto resolved_cam = resolve_camera_hierarchy(result.layers(), result.resource(), cam);
        result.set_camera_2_5d(std::move(resolved_cam.camera));
    }

    return result;
}

} // namespace chronon3d
