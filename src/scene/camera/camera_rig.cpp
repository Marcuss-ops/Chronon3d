#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d {

static Vec3 orbit_offset(f32 yaw_deg, f32 pitch_deg, f32 radius) {
    const f32 yaw   = glm::radians(yaw_deg);
    const f32 pitch = glm::radians(pitch_deg);

    return Vec3{
        std::sin(yaw) * std::cos(pitch) * radius,
        std::sin(pitch) * radius,
        -std::cos(yaw) * std::cos(pitch) * radius
    };
}

Camera2_5D CameraRig::evaluate(
    Frame frame,
    const TransformResolverResult* resolved
) const {
    Camera2_5D cam;
    cam.enabled = true;
    cam.projection_mode = projection_mode;

    Vec3 resolved_target = target.evaluate(frame);

    if (resolved && !target_name.empty()) {
        if (auto target_world = resolved->world_position(target_name)) {
            resolved_target = *target_world;
        }
    }

    const f32 yaw    = orbit_yaw.evaluate(frame);
    const f32 pitch  = orbit_pitch.evaluate(frame);
    const f32 radius = orbit_radius.evaluate(frame);

    Vec3 pos = resolved_target + orbit_offset(yaw, pitch, radius);

    pos += track.evaluate(frame);

    const Vec3 forward = glm::normalize(resolved_target - pos);
    pos += forward * dolly.evaluate(frame);

    if (resolved && !parent_name.empty()) {
        if (auto parent_matrix = resolved->world_matrix(parent_name)) {
            pos = Vec3((*parent_matrix) * Vec4(pos, 1.0f));
            resolved_target = Vec3((*parent_matrix) * Vec4(resolved_target, 1.0f));
        }
    }

    cam.position = pos;
    cam.zoom = zoom.evaluate(frame);
    cam.fov_deg = fov_deg.evaluate(frame);

    if (mode == CameraRigMode::TwoNode) {
        cam.point_of_interest = resolved_target;
        cam.point_of_interest_enabled = true;
        cam.rotation.z = roll.evaluate(frame);
    } else {
        cam.point_of_interest_enabled = false;
        cam.rotation = Vec3{
            tilt.evaluate(frame),
            pan.evaluate(frame),
            roll.evaluate(frame)
        };
    }

    cam.dof.enabled = dof.enabled;
    cam.dof.focus_z = dof.use_target_z ? resolved_target.z : dof.focus_z.evaluate(frame);
    cam.dof.aperture = dof.aperture.evaluate(frame);
    cam.dof.max_blur = dof.max_blur.evaluate(frame);

    return cam;
}

namespace camera_rig {
    void CameraRig::apply(SceneBuilder& s, Frame frame, std::function<void(SceneBuilder&)> add_target_content) const {
        s.null_layer(controller_name, [&](LayerBuilder& l) {
            l.position(controller_position.evaluate(frame))
             .rotate(controller_rotation.evaluate(frame))
             .anchor(controller_anchor.evaluate(frame));
        });

        s.null_layer(target_name, [&](LayerBuilder& l) {
            l.position(target_position.evaluate(frame))
             .rotate(target_rotation.evaluate(frame))
             .anchor(target_anchor.evaluate(frame));
        });

        add_target_content(s);
        s.camera().set(bake(frame, s.resource()));
    }

    void CameraRig::apply(SceneBuilder& s, Frame frame) const {
        apply(s, frame, [](SceneBuilder&) {});
    }
}

} // namespace chronon3d
