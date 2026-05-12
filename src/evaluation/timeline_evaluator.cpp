#include <chronon3d/evaluation/timeline_evaluator.hpp>
#include <chronon3d/math/quat.hpp>
#include <variant>

namespace chronon3d {

namespace {

// Convert euler rotation (degrees, XYZ order) to Quat.
inline Quat euler_deg_to_quat(Vec3 euler_deg) {
    const Vec3 r = glm::radians(glm::vec3(euler_deg.x, euler_deg.y, euler_deg.z));
    return Quat(r);
}

// Resolve the world-space Z for a layer given its depth role and explicit Z.
inline f32 resolve_z(const LayerDesc& l, Vec3 evaluated_pos) {
    if (l.is_3d && l.depth_role != DepthRole::None) {
        return DepthRoleResolver::z_for(l.depth_role) + l.depth_offset;
    }
    return evaluated_pos.z;
}

// Map EffectDesc entries to the flat LayerEffect struct.
inline LayerEffect resolve_effects(const std::vector<EffectDesc>& descs) {
    LayerEffect eff;
    for (const auto& d : descs) {
        std::visit([&eff](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlurEffectDesc>) {
                eff.blur_radius = e.radius;
            } else if constexpr (std::is_same_v<T, TintEffectDesc>) {
                eff.tint = e.color;
            } else if constexpr (std::is_same_v<T, BrightnessContrastEffectDesc>) {
                eff.brightness = e.brightness;
                eff.contrast   = e.contrast;
            }
            // DropShadow / Glow: per-RenderNode; handled by LegacySceneAdapter.
        }, d);
    }
    return eff;
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
        el.opacity    = ld.opacity.value_at(frame);
        el.is_3d      = ld.is_3d;
        el.depth_role = ld.depth_role;
        el.blend_mode = ld.blend_mode;

        // Resolve animated transform
        Vec3 pos = ld.position.value_at(frame);
        Vec3 rot = ld.rotation.value_at(frame);  // euler degrees
        Vec3 scl = ld.scale.value_at(frame);

        el.resolved_z      = resolve_z(ld, pos);
        pos.z              = el.resolved_z;

        el.world_transform.position = pos;
        el.world_transform.rotation = euler_deg_to_quat(rot);
        el.world_transform.scale    = scl;
        el.world_transform.opacity  = el.opacity;

        // Copy static visuals
        el.visuals = ld.visuals;

        // Resolve effects into flat struct
        el.resolved_effect = resolve_effects(ld.effects);

        result.layers.push_back(std::move(el));
    }

    // Resolve camera
    if (scene.camera && scene.camera->enabled) {
        Camera2_5D cam;
        cam.enabled            = true;
        cam.position           = scene.camera->position.value_at(frame);
        cam.point_of_interest  = scene.camera->point_of_interest;
        cam.zoom               = scene.camera->zoom;
        result.camera          = cam;
    }

    return result;
}

} // namespace chronon3d
