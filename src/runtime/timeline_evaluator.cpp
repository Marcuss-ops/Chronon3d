#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/expression.hpp>
#include <chronon3d/animation/wiggle.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
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
            } else if constexpr (std::is_same_v<T, SaturationEffectDesc>) {
                stack.push_back(EffectInstance{SaturationParams{e.value}});
            } else if constexpr (std::is_same_v<T, HueRotateEffectDesc>) {
                stack.push_back(EffectInstance{HueRotateParams{e.degrees}});
            } else if constexpr (std::is_same_v<T, InvertEffectDesc>) {
                stack.push_back(EffectInstance{InvertParams{e.amount}});
            } else if constexpr (std::is_same_v<T, VignetteEffectDesc>) {
                stack.push_back(EffectInstance{VignetteParams{e.radius, e.softness, e.amount}});
            } else if constexpr (std::is_same_v<T, DropShadowEffectDesc>) {
                stack.push_back(EffectInstance{DropShadowParams{e.offset, e.color, e.radius}});
            } else if constexpr (std::is_same_v<T, GlowEffectDesc>) {
                stack.push_back(EffectInstance{GlowParams{.radius = e.radius, .intensity = e.intensity, .color = e.color}});
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

// ── Build a cross-layer property resolver ────────────────────────────────
// Returns a lambda that, given (layer_name, property_path, time), evaluates
// the requested property of another layer in the same composition.

using LayerResolver = math::ExpressionContext::LayerPropertyResolver;

inline LayerResolver build_layer_resolver(const std::vector<LayerDesc>& all_layers) {
    // Build a name -> index lookup
    std::unordered_map<std::string, size_t> name_to_idx;
    for (size_t i = 0; i < all_layers.size(); ++i) {
        name_to_idx[all_layers[i].name] = i;
    }

    // Capture by value so the lambda is self-contained
    return [layers = all_layers, name_to_idx](
            const std::string& layer_name,
            const std::string& prop_path,
            double /*time*/) -> double {

        auto it = name_to_idx.find(layer_name);
        if (it == name_to_idx.end()) return std::numeric_limits<double>::quiet_NaN();

        const LayerDesc& ld = layers[it->second];

        // Resolve common property paths.
        // For a full implementation we'd need to evaluate AnimatedValue at the
        // given time.  For now, we evaluate at the current keyframed value.
        // This covers the most common use cases: layer("bg").opacity, etc.
        Frame eval_frame{0}; // Will be refined by context

        if (prop_path == "transform.opacity" || prop_path == "opacity") {
            return static_cast<double>(ld.opacity.value_at(Frame{0}));
        }
        if (prop_path == "transform.position" || prop_path == "position") {
            return static_cast<double>(ld.position.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.position.x" || prop_path == "position.x") {
            return static_cast<double>(ld.position.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.position.y" || prop_path == "position.y") {
            return static_cast<double>(ld.position.value_at(Frame{0}).y);
        }
        if (prop_path == "transform.position.z" || prop_path == "position.z") {
            return static_cast<double>(ld.position.value_at(Frame{0}).z);
        }
        if (prop_path == "transform.scale" || prop_path == "scale") {
            return static_cast<double>(ld.scale.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.scale.x" || prop_path == "scale.x") {
            return static_cast<double>(ld.scale.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.scale.y" || prop_path == "scale.y") {
            return static_cast<double>(ld.scale.value_at(Frame{0}).y);
        }
        if (prop_path == "transform.scale.z" || prop_path == "scale.z") {
            return static_cast<double>(ld.scale.value_at(Frame{0}).z);
        }
        if (prop_path == "transform.rotation" || prop_path == "rotation") {
            return static_cast<double>(ld.rotation.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.rotation.x" || prop_path == "rotation.x") {
            return static_cast<double>(ld.rotation.value_at(Frame{0}).x);
        }
        if (prop_path == "transform.rotation.y" || prop_path == "rotation.y") {
            return static_cast<double>(ld.rotation.value_at(Frame{0}).y);
        }
        if (prop_path == "transform.rotation.z" || prop_path == "rotation.z") {
            return static_cast<double>(ld.rotation.value_at(Frame{0}).z);
        }

        // Unknown property
        return std::numeric_limits<double>::quiet_NaN();
    };
}

} // namespace

Scene TimelineEvaluator::evaluate(const SceneDescription& scene, Frame frame, std::pmr::memory_resource* res) const {
    Scene result(res);

    // Build cross-layer resolver for AE-style layer("name").property expressions
    LayerResolver layer_resolver = build_layer_resolver(scene.layers);

    for (size_t layer_idx = 0; layer_idx < scene.layers.size(); ++layer_idx) {
        const auto& ld = scene.layers[layer_idx];
        if (!ld.time_range.contains(frame)) continue;

        Layer layer(res);
        layer.name         = std::pmr::string{ld.name, res};
        layer.kind         = ld.kind;

        // Time Remap (AE-4)
        layer.time_remap.speed = ld.time_remap_speed;
        layer.time_remap.freeze_frame = ld.time_remap_freeze_frame;
        if (ld.time_remap_curve.is_animated()) {
            layer.time_remap.time_remap = ld.time_remap_curve;
        }
        layer.uses_2_5d_projection = ld.is_3d;
        layer.depth_role   = ld.depth_role;
        layer.blend_mode   = ld.blend_mode;
        layer.effects      = resolve_effects(ld.effects);
        layer.transition_in = ld.transition_in;
        layer.transition_out = ld.transition_out;
        layer.visible      = true;

        // Precomp layers: propagate composition name
        if (ld.kind == LayerKind::Precomp) {
            layer.precomp_composition_name = std::pmr::string{ld.precomp_composition_name, res};
        }

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);

        // Build ExpressionContext for this layer (enables cross-layer refs)
        math::ExpressionContext ectx;
        ectx.frame        = static_cast<double>(frame);
        ectx.time         = time;
        ectx.fps          = static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator;
        ectx.width        = static_cast<double>(scene.width);
        ectx.height       = static_cast<double>(scene.height);
        ectx.num_layers   = static_cast<double>(scene.layers.size());
        ectx.index        = static_cast<double>(layer_idx + 1); // 1-based
        ectx.layer_resolver = layer_resolver;

        AnimationEvalContext actx;
        actx.fps = static_cast<f32>(ectx.fps);
        actx.time = static_cast<f32>(time);
        actx.has_explicit_time = true;
        actx.index = static_cast<int>(layer_idx + 1);
        actx.expression_context = &ectx;

        f32 opacity = 1.0f;
        if (ld.opacity.has_expression()) {
            opacity = ld.opacity.evaluate(frame, actx);
        } else {
            opacity = ld.opacity.value_at(frame);
        }

        Vec3 pos = ld.position.evaluate(frame, actx);
        Vec3 rot = ld.rotation.evaluate(frame, actx);
        Vec3 scl = ld.scale.evaluate(frame, actx);

        f32 resolved_z = resolve_z(ld, pos);
        pos.z = resolved_z;

        layer.transform.position = pos;
        layer.transform.rotation = math::camera_rotation_quat(rot);
        layer.transform.scale    = scl;
        layer.transform.opacity  = opacity;

        // Only Normal/Video/Glass layers produce their own visual nodes
        if (ld.kind == LayerKind::Normal || ld.kind == LayerKind::Video || ld.kind == LayerKind::Glass) {
            for (const auto& vd : ld.visuals) {
                layer.nodes.push_back(make_render_node(vd, layer.transform, res));
            }
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
