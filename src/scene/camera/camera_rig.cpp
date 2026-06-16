#include <chronon3d/scene/builders/scene_builder.hpp>
#include <spdlog/spdlog.h>

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

// ── Sub-frame evaluation (primary implementation) ────────────────────────
Camera2_5D CameraRig::evaluate(
    SampleTime time,
    const TransformResolverResult* resolved
) const {
    Camera2_5D cam;
    cam.enabled = true;
    cam.projection_mode = projection_mode;

    Vec3 resolved_target = target.evaluate(time);

    // ── Target resolution: bindings → legacy name fallback ─────────────
    CameraBindingResolver binding_resolver(resolved);

    if (resolved) {
        if (!target_bindings.empty()) {
            if (auto blended = binding_resolver.resolve_blend(target_bindings)) {
                resolved_target = *blended;
            }
        } else if (!target_name.empty()) {
            if (auto target_world = resolved->world_position(target_name)) {
                resolved_target = *target_world;
            }
        }
    }

    const f32 yaw    = orbit_yaw.evaluate(time);
    const f32 pitch  = orbit_pitch.evaluate(time);
    const f32 radius = orbit_radius.evaluate(time);

    Vec3 pos = resolved_target + orbit_offset(yaw, pitch, radius);

    pos += track.evaluate(time);

    const Vec3 forward = glm::normalize(resolved_target - pos);
    pos += forward * dolly.evaluate(time);

    if (resolved && !parent_name.empty()) {
        if (auto parent_matrix = resolved->world_matrix(parent_name)) {
            pos = Vec3((*parent_matrix) * Vec4(pos, 1.0f));
            resolved_target = Vec3((*parent_matrix) * Vec4(resolved_target, 1.0f));
        }
    }

    cam.position = pos;
    cam.zoom = zoom.evaluate(time);
    cam.fov_deg = fov_deg.evaluate(time);

    if (mode == CameraRigMode::TwoNode) {
        cam.point_of_interest = resolved_target;
        cam.point_of_interest_enabled = true;
        cam.rotation.z = roll.evaluate(time);
        cam.orientation = math::camera_rotation_quat(cam.rotation);
        cam.orientation_valid = true;
    } else {
        cam.point_of_interest_enabled = false;
        cam.rotation = Vec3{
            tilt.evaluate(time),
            pan.evaluate(time),
            roll.evaluate(time)
        };
        cam.orientation = math::camera_rotation_quat(cam.rotation);
        cam.orientation_valid = true;
    }

    cam.dof.enabled = dof.enabled;

    // ── Focus resolution ────────────────────────────────────────────────
    // Resolve focus plane using the declared focus_mode.  The three modes
    // are mutually exclusive — no fallback assignment can overwrite the
    // resolved value (fixes the focus-target overwrite bug).
    cam.dof.use_physical_model = dof.use_physical_model;

    switch (dof.focus_mode) {
        case CameraFocusMode::TargetBinding: {
            Vec3 focus_target_world = resolved_target;
            if (resolved) {
                // Prefer unified binding; fall back to legacy string.
                if (!dof.focus_binding.empty()) {
                    if (auto ft = binding_resolver.resolve_position(dof.focus_binding)) {
                        focus_target_world = *ft;
                    } else {
                        spdlog::warn("CameraRig '{}': focus_binding '{}' not found in resolver, "
                                     "falling back to rig target",
                                     name, dof.focus_binding.name);
                    }
                } else if (!dof.focus_target_name.empty()) {
                    if (auto ft_world = resolved->world_position(dof.focus_target_name)) {
                        focus_target_world = *ft_world;
                    } else {
                        spdlog::warn("CameraRig '{}': focus_target '{}' not found in resolver, "
                                     "falling back to rig target",
                                     name, dof.focus_target_name);
                    }
                }
            }
            // Camera-space distance — correct for orbiting/rotated cameras.
            cam.dof.focus_distance = glm::length(focus_target_world - pos);
            cam.dof.focus_z = focus_target_world.z;
            break;
        }
        case CameraFocusMode::ManualDistance: {
            cam.dof.focus_distance = dof.focus_distance.evaluate(time);
            cam.dof.focus_z = dof.focus_z.evaluate(time);
            break;
        }
        case CameraFocusMode::LegacyWorldZ: {
            cam.dof.focus_z = dof.focus_z.evaluate(time);
            break;
        }
        default:
            break;
    }

    cam.dof.aperture = dof.aperture.evaluate(time);
    cam.dof.max_blur = dof.max_blur.evaluate(time);

    // Populate physical lens model from CameraRigDOF.
    cam.lens.focal_length   = dof.focal_length.evaluate(time);
    cam.lens.sensor_width   = dof.sensor_width.evaluate(time);
    cam.lens.sensor_height  = dof.sensor_height.evaluate(time);
    cam.lens.f_stop         = dof.f_stop.evaluate(time);
    cam.lens.close_focus    = dof.close_focus.evaluate(time);
    cam.lens.gate_fit       = dof.gate_fit;

    // ── Mark camera as animated (local properties + external dependencies) ──
    const bool has_local_animation =
        target.is_time_dependent() || orbit_yaw.is_time_dependent() ||
        orbit_pitch.is_time_dependent() || orbit_radius.is_time_dependent() ||
        track.is_time_dependent() || dolly.is_time_dependent() ||
        pan.is_time_dependent() || tilt.is_time_dependent() || roll.is_time_dependent() ||
        zoom.is_time_dependent() || fov_deg.is_time_dependent() ||
        dof.focus_z.is_time_dependent() || dof.aperture.is_time_dependent() || dof.max_blur.is_time_dependent() ||
        dof.focal_length.is_time_dependent() || dof.sensor_width.is_time_dependent() ||
        dof.sensor_height.is_time_dependent() || dof.f_stop.is_time_dependent() ||
        dof.close_focus.is_time_dependent() || dof.focus_distance.is_time_dependent();

    // External dependencies: parent_name, target_name, target_bindings,
    // and focus_target_name/focus_binding may reference layers with animated
    // transforms.  When any external reference is present, conservatively
    // treat the camera as animated to prevent stale cache hits.
    const bool has_external_dependencies =
        !parent_name.empty() || !target_name.empty() ||
        !target_bindings.empty() ||
        !dof.focus_target_name.empty() || !dof.focus_binding.empty();

    cam.is_animated = has_local_animation || has_external_dependencies;

    // ── Propagate motion blur from rig to camera ────────────────────────────
    cam.motion_blur.enabled          = motion_blur.enabled;
    cam.motion_blur.samples          = motion_blur.samples;
    cam.motion_blur.shutter_angle_deg = motion_blur.shutter_angle_deg;
    cam.motion_blur.shutter_phase_deg = motion_blur.shutter_phase_deg;
    cam.motion_blur.pattern          = motion_blur.pattern;
    cam.motion_blur.filter           = motion_blur.filter;
    cam.motion_blur.jitter_seed      = motion_blur.jitter_seed;

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
