#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/registry/shape_ids.hpp>

#include <algorithm>
#include <utility>

namespace chronon3d::presets::motion {

namespace {

MotionState compose_state(const MotionState& parent, const MotionState& child) {
    MotionState out = child;
    out.visible = parent.visible && child.visible;
    out.position = parent.position + child.position;
    out.scale = {parent.scale.x * child.scale.x, parent.scale.y * child.scale.y, parent.scale.z * child.scale.z};
    out.rotation = parent.rotation + child.rotation;
    out.opacity = parent.opacity * child.opacity;
    out.blur = std::max(parent.blur, child.blur);
    return out;
}

std::string prefix_name(const std::string& prefix, const std::string& id) {
    if (prefix.empty()) {
        return id;
    }
    return prefix + "__" + id;
}

void apply_state(LayerBuilder& l, const MotionState& st, bool enable_3d) {
    if (enable_3d) {
        l.enable_3d();
    }
    l.position(st.position)
     .rotate(st.rotation)
     .scale(st.scale)
     .opacity(st.opacity);
    if (st.blur > 0.0f) {
        l.blur(st.blur);
    }
}

Vec3 face_camera_rotation(const SceneBuilder& s, bool face_camera) {
    if (!face_camera) {
        return {0.0f, 0.0f, 0.0f};
    }

    // Cancel the current camera rotation so text and cards remain front-facing.
    return -s.camera_2_5d().rotation_euler();
}

void draw_content(LayerBuilder& l, const MotionObject& obj, const std::string& layer_name) {
    switch (obj.type) {
    case MotionObjectType::Text:
        l.shape(chronon3d::registry::shape_ids::Text, layer_name + "_text", chronon3d::TextParams{
            .text = obj.text_value,
            .size = obj.size_value,
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = obj.text_style.font_path,
            .font_family = obj.text_style.font_family,
            .font_weight = obj.text_style.font_weight,
            .font_style = obj.text_style.font_style,
            .font_size = obj.text_style.font_size,
            .color = obj.color_value,
            .align = obj.text_style.align == TextAlign::Left
                ? chronon3d::TextAlign::Left
                : obj.text_style.align == TextAlign::Right
                    ? chronon3d::TextAlign::Right
                    : chronon3d::TextAlign::Center,
            .line_height = 1.2f,
            .tracking = obj.text_style.tracking,
        });
        break;
    case MotionObjectType::Image:
        l.image(layer_name + "_image", {
            .path = obj.image_path_value,
            .size = obj.size_value,
            .pos = {0.0f, 0.0f, 0.0f},
            .opacity = 1.0f,
        });
        break;
    case MotionObjectType::Rect:
        l.rect(layer_name + "_rect", {
            .size = obj.size_value,
            .color = obj.color_value.with_alpha(1.0f),
            .pos = {0.0f, 0.0f, 0.0f},
        });
        break;
    case MotionObjectType::RoundedRect:
        l.rounded_rect(layer_name + "_rounded_rect", {
            .size = obj.size_value,
            .radius = obj.radius_value,
            .color = obj.color_value.with_alpha(1.0f),
            .pos = {0.0f, 0.0f, 0.0f},
        });
        break;
    case MotionObjectType::Circle:
        l.circle(layer_name + "_circle", {
            .radius = obj.size_value.x * 0.5f,
            .color = obj.color_value.with_alpha(1.0f),
            .pos = {0.0f, 0.0f, 0.0f},
        });
        break;
    case MotionObjectType::Line:
        l.line(layer_name + "_line", {
            .from = obj.line_from_value,
            .to = obj.line_to_value,
            .thickness = obj.line_thickness_value,
            .color = obj.color_value.with_alpha(1.0f),
        });
        break;
    case MotionObjectType::Group:
        break;
    }
}

void draw_motion_object_impl(
    SceneBuilder& s,
    const FrameContext& ctx,
    const MotionObject& obj,
    const MotionState& inherited,
    bool inherited_3d,
    const std::string& prefix
) {
    MotionState st = compose_state(inherited, resolve_motion_state(ctx, obj));
    const bool enable_3d = inherited_3d || obj.motion3d.enabled;
    if (!st.visible || st.opacity <= 0.0f) {
        return;
    }

    if (obj.type == MotionObjectType::Group) {
        const std::string child_prefix = prefix_name(prefix, obj.id);
        for (const auto& child : obj.children) {
            draw_motion_object_impl(s, ctx, child, st, enable_3d, child_prefix);
        }
        return;
    }

    const std::string layer_name = prefix_name(prefix, obj.id);


    const Vec3 face_cam_rot = face_camera_rotation(s, obj.motion3d.face_camera);

    s.layer(layer_name, [obj, st, layer_name, enable_3d, face_cam_rot](LayerBuilder& l) {
        apply_state(l, st, enable_3d);
        if (obj.motion3d.face_camera) {
            l.rotate(st.rotation + face_cam_rot);
        }
        l.opacity(st.opacity * obj.color_value.a);
        draw_content(l, obj, layer_name);
        if (obj.glow_enabled) {
            l.with_glow(Glow{
                .enabled = true,
                .radius = 12.0f,
                .intensity = 0.65f,
                .color = obj.color_value.with_alpha(1.0f),
            });
        }
    });
}

} // namespace

void draw_motion_object(SceneBuilder& s, const FrameContext& ctx, const MotionObject& obj) {
    draw_motion_object_impl(s, ctx, obj, MotionState{}, false, "");
}

void draw_motion_objects(SceneBuilder& s, const FrameContext& ctx, const std::vector<MotionObject>& objects) {
    for (const auto& obj : objects) {
        draw_motion_object(s, ctx, obj);
    }
}

} // namespace chronon3d::presets::motion
