#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

// ── evaluate(Frame) wrapper (was inline in header) ───────────────────────
Camera2_5D CameraRig::evaluate(
    Frame frame,
    const ResolvedSceneTransforms* resolved
) const {
    return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}), resolved);
}

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
    const ResolvedSceneTransforms* resolved
) const {
    Camera2_5D cam;
    cam.enabled = true;
    cam.optics_mode = optics_mode;

    Vec3 resolved_target = target.evaluate(time);
    bool target_from_hierarchy = false;

    if (resolved && !target_name.empty()) {
        if (auto target_world = resolved->world_position(target_name)) {
            resolved_target = *target_world;
            target_from_hierarchy = true;  // target is in WORLD space
        }
    }

    const f32 yaw    = orbit_yaw.evaluate(time);
    const f32 pitch  = orbit_pitch.evaluate(time);
    const f32 radius = orbit_radius.evaluate(time);

    Vec3 pos = resolved_target + orbit_offset(yaw, pitch, radius);
    pos += track.evaluate(time);

    // Apply parent transform:
    //   - Camera position (pos): ALWAYS follows parent ("camera trasformata dal parent")
    //   - Target: transformed ONLY when in local space (not from hierarchy)
    //     When target_from_hierarchy is true, resolved_target is already in
    //     WORLD space — applying parent would double-transform it.
    if (resolved && !parent_name.empty()) {
        if (auto parent_matrix = resolved->world_matrix(parent_name)) {
            pos = Vec3((*parent_matrix) * Vec4(pos, 1.0f));
            if (!target_from_hierarchy) {
                resolved_target = Vec3((*parent_matrix) * Vec4(resolved_target, 1.0f));
            }
        }
    }

    // Dolly: push camera along world-space forward AFTER parent transform.
    {
        const Vec3 diff = resolved_target - pos;
        if (glm::length(diff) > 0.0001f) {
            pos += (diff / glm::length(diff)) * dolly.evaluate(time);
        }
    }

    cam.position = pos;
    cam.zoom = zoom.evaluate(time);
    cam.fov_deg = fov_deg.evaluate(time);

    if (mode == CameraRigMode::TwoNode) {
        cam.point_of_interest = resolved_target;
        cam.point_of_interest_enabled = true;
        // Canonical TwoNode factorization at the Camera2_5D level:
        //   q = qLookAt * qLocal * qX(tilt) * qY(pan) * qZ(roll).
        // All three Euler factors are forwarded so the rig matches the AE
        // TwoNode + Pan/Tilt/Roll contract.  The lookAt quaternion is then
        // composed by Camera2_5D::resolve_look_at_orientation().
        cam.rotation = Vec3{
            tilt.evaluate(time),
            pan.evaluate(time),
            roll.evaluate(time)
        };
    } else {
        cam.point_of_interest_enabled = false;
        cam.rotation = Vec3{
            tilt.evaluate(time),
            pan.evaluate(time),
            roll.evaluate(time)
        };
    }

    // ── Propagate motion blur from rig to camera ────────────────
    // TICKET-026 — `cam.motion_blur.enabled = motion_blur.enabled` is gone.
    // The mode field is the canonical "active?" indicator now.
    cam.motion_blur.mode            = motion_blur.mode;
    cam.motion_blur.samples          = motion_blur.samples;
    cam.motion_blur.shutter_angle_deg = motion_blur.shutter_angle_deg;
    cam.motion_blur.shutter_phase_deg = motion_blur.shutter_phase_deg;
    cam.motion_blur.pattern          = motion_blur.pattern;
    cam.motion_blur.filter           = motion_blur.filter;
    cam.motion_blur.jitter_seed      = motion_blur.jitter_seed;

    // ── Focus: single switch is responsible for focus_distance ────────────
    //
    // The previous implementation always evaluated focus_distance last,
    // silently overwriting the target-derived distance.  The canonical
    // contract routes the assignment through one and only one path.
    auto focusMode = dof.focus_mode;
    if (dof.use_target_z) {
        // Backward-compat: legacy boolean now maps onto FocusMode.
        focusMode = !dof.focus_target_name.empty()
            ? CameraFocusMode::TargetLayer
            : CameraFocusMode::PointOfInterest;
    }

    // Compute the candidate POI world position (camera-space distance).
    Vec3 po_for_focus = resolved_target;
    bool po_resolved_from_layer = false;
    if (focusMode == CameraFocusMode::TargetLayer &&
        resolved && !dof.focus_target_name.empty())
    {
        if (auto ft_world = resolved->world_position(dof.focus_target_name)) {
            po_for_focus = *ft_world;
            po_resolved_from_layer = true;
        } else {
            spdlog::warn("CameraRig '{}': focus_target '{}' not found in resolver, "
                         "falling back to PointOfInterest",
                         name, dof.focus_target_name);
        }
    }

    switch (focusMode) {
        case CameraFocusMode::ManualDistance: {
            // Explicit value; explicit owner wins, target is ignored.
            cam.dof.focus_distance = dof.focus_distance.evaluate(time);
            cam.dof.focus_z        = dof.focus_z.evaluate(time);
            break;
        }
        case CameraFocusMode::PointOfInterest: {
            cam.dof.focus_distance = glm::length(po_for_focus - pos);
            cam.dof.focus_z        = po_for_focus.z;
            break;
        }
        case CameraFocusMode::TargetLayer: {
            if (po_resolved_from_layer) {
                cam.dof.focus_distance = glm::length(po_for_focus - pos);
                cam.dof.focus_z        = po_for_focus.z;
            } else {
                // Fallback to POI distance for missing layer.
                cam.dof.focus_distance = glm::length(resolved_target - pos);
                cam.dof.focus_z        = resolved_target.z;
            }
            break;
        }
        case CameraFocusMode::LockToZoom: {
            // Dolly-zoom / rack: pin focus to current zoom.
            cam.dof.focus_distance = cam.zoom;
            cam.dof.focus_z        = po_for_focus.z;
            break;
        }
    }
    // NOTE: do NOT re-assign cam.dof.focus_distance below this switch.
    // Any sample that re-evaluates the manual AnimatedValue here would
    // silently overwrite the focused values: that is the bug being fixed.

    cam.dof.aperture = dof.aperture.evaluate(time);
    cam.dof.max_blur = dof.max_blur.evaluate(time);

    // Populate physical lens model from CameraRigDOF.
    cam.lens.focal_length   = dof.focal_length.evaluate(time);
    cam.lens.sensor_width   = dof.sensor_width.evaluate(time);
    cam.lens.sensor_height  = dof.sensor_height.evaluate(time);
    cam.lens.f_stop         = dof.f_stop.evaluate(time);
    cam.lens.close_focus    = dof.close_focus.evaluate(time);
    cam.lens.gate_fit       = dof.gate_fit;

    // Optics mode is mirrored from the rig; DoF physical-model flag is
    // independent (optics != DoF, AE-style).
    cam.dof.use_physical_model  = dof.use_physical_model;

    // ── Mark camera as animated ───────────────────────────────────────────
    // The previous implementation only considered local AnimatedValues.
    // External references (parent_name, target_name, focus_target_name)
    // must also invalidate caches, since the world transform they pull
    // from may move even when the rig itself has no keyframes.  We use a
    // conservative fallback: if any external reference is bound, the
    // rig is considered time-dependent.
    const bool local_animated =
        target.is_time_dependent() || orbit_yaw.is_time_dependent() ||
        orbit_pitch.is_time_dependent() || orbit_radius.is_time_dependent() ||
        track.is_time_dependent() || dolly.is_time_dependent() ||
        pan.is_time_dependent() || tilt.is_time_dependent() || roll.is_time_dependent() ||
        zoom.is_time_dependent() || fov_deg.is_time_dependent() ||
        dof.focus_z.is_time_dependent() || dof.aperture.is_time_dependent() || dof.max_blur.is_time_dependent() ||
        dof.focal_length.is_time_dependent() || dof.sensor_width.is_time_dependent() ||
        dof.sensor_height.is_time_dependent() || dof.f_stop.is_time_dependent() ||
        dof.close_focus.is_time_dependent() || dof.focus_distance.is_time_dependent();
    const bool external_dep = has_external_dependencies();
    cam.is_animated = local_animated || external_dep;

    // ── Propagate motion blur from rig to camera ────────────────────────────
    // TICKET-026 — `MotionBlurSettings::enabled` removed; `mode` is the
    // canonical active indicator.  Mirror the rig-side mode verbatim; helper
    // `is_motion_blur_active(settings)` reads it for downstream consumers.
    cam.motion_blur.mode             = motion_blur.mode;
    cam.motion_blur.samples          = motion_blur.samples;
    cam.motion_blur.shutter_angle_deg = motion_blur.shutter_angle_deg;
    cam.motion_blur.shutter_phase_deg = motion_blur.shutter_phase_deg;
    cam.motion_blur.pattern          = motion_blur.pattern;
    cam.motion_blur.filter           = motion_blur.filter;
    cam.motion_blur.jitter_seed      = motion_blur.jitter_seed;

    return cam;
}

} // namespace chronon3d
