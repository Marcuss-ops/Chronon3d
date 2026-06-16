#pragma once

#include <chronon3d/animation/core/animated_quaternion.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

/// AnimatedCamera2_5D — a Camera2_5D where every key property is animatable
/// via keyframed AnimatedValue<T> fields.
///
/// Orientation: use .orientation (Quat, SLERP) for gimbal-lock-free animation.
/// The legacy .rotation (Euler Vec3) is kept for backward compat. When both
/// are used, orientation wins and rotation_offset_euler is applied additively.
///
/// Usage:
///   AnimatedCamera2_5D cam;
///   cam.position
///       .key(0,   Vec3{0,0,-1200})
///       .key(90,  Vec3{0,0,-800},  EasingCurve{Easing::OutCubic});
///   cam.orientation.key(0, Quat{1,0,0,0})
///                   .key(90, glm::angleAxis(glm::radians(180.f), Vec3{0,1,0}));
///   Camera2_5D static_cam = cam.evaluate(45);
struct AnimatedCamera2_5D {
    bool enabled{true};

    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, -1000.0f}};

    /// Primary orientation — Quat keyframes with SLERP interpolation.
    /// When animated, this takes precedence over the legacy `rotation` field.
    AnimatedQuaternion orientation{Quat{1.0f, 0.0f, 0.0f, 0.0f}};

    /// Local rotation offset applied after orientation (AE-style).
    /// Animates pan/tilt/roll separately from the main orientation pose.
    /// Only meaningful when orientation is keyframed.
    AnimatedValue<Vec3> rotation_offset_euler{Vec3{0.0f, 0.0f, 0.0f}};

    /// Legacy Euler rotation (backward compat). Automatically synced to
    /// orientation when orientation is static. When orientation is animated,
    /// rotation is set from the resolved orientation value.
    AnimatedValue<Vec3> rotation{Vec3{0.0f, 0.0f, 0.0f}};

    AnimatedValue<f32>  zoom{1000.0f};
    AnimatedValue<f32>  fov_deg{50.0f};

    AnimatedValue<Vec3> point_of_interest{Vec3{0.0f, 0.0f, 0.0f}};
    bool                point_of_interest_enabled{false};

    // Depth of field: z-plane in focus. Animatable for rack-focus effects.
    AnimatedValue<f32>  focus_z{0.0f};
    AnimatedValue<f32>  aperture{0.015f};
    AnimatedValue<f32>  max_blur{24.0f};

    // Separate enable flag — DoF can be enabled with constant values,
    // independent of whether the parameters are keyframed.
    bool  dof_enabled{false};

    // Physical lens parameters (used when dof.use_physical_model is true).
    // These mirror the fields in LensModel for animation support.
    AnimatedValue<f32>  focal_length{50.0f};
    AnimatedValue<f32>  sensor_width{36.0f};
    AnimatedValue<f32>  sensor_height{24.0f};
    AnimatedValue<f32>  f_stop{2.8f};
    AnimatedValue<f32>  focus_distance{1000.0f};
    AnimatedValue<f32>  close_focus{450.0f};
    GateFit             gate_fit{GateFit::Fill};
    bool  use_physical_model{false};

    /// Evaluate all animated properties at `frame` and return a static Camera2_5D.
    [[nodiscard]] Camera2_5D evaluate(Frame frame) const {
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}));
    }

    /// Sub-frame evaluation — enables true multi-sample motion blur.
    [[nodiscard]] Camera2_5D evaluate(SampleTime time) const {
        Camera2_5D cam;
        cam.enabled = enabled;
        cam.is_animated = is_time_dependent();
        cam.position = position.evaluate(time);

        // ── Orientation resolution ────────────────────────────────────────
        // Primary: Quat orientation → apply rotation_offset_euler → set cam.
        // Legacy: Euler rotation → convert to Quat → set cam (backward compat).
        if (orientation.is_animated()) {
            Quat base_orient = orientation.evaluate(time);
            Vec3 offset_euler = rotation_offset_euler.evaluate(time);
            if (glm::length2(offset_euler) > 1e-12f) {
                const Quat offset_quat = math::camera_rotation_quat(offset_euler);
                base_orient = glm::normalize(base_orient * offset_quat);
            }
            cam.orientation = base_orient;
            cam.orientation_valid = true;
            cam.rotation = glm::degrees(glm::eulerAngles(base_orient));
        } else if (rotation.is_animated()) {
            cam.rotation = rotation.evaluate(time);
            cam.orientation = math::camera_rotation_quat(cam.rotation);
            cam.orientation_valid = true;
        } else {
            // Non-animated: orientation explicitly set wins over rotation.
            Quat orient_default = orientation.evaluate(time);
            bool orient_explicit = glm::length2(orient_default - Quat{1,0,0,0}) > 1e-12f;
            if (orient_explicit) {
                cam.orientation = orient_default;
                cam.rotation = math::camera_rotation_euler(orient_default);
            } else {
                cam.rotation = rotation.evaluate(time);
                cam.orientation = math::camera_rotation_quat(cam.rotation);
            }
            cam.orientation_valid = true;
        }

        cam.zoom     = zoom.evaluate(time);
        cam.fov_deg  = fov_deg.evaluate(time);

        cam.point_of_interest          = point_of_interest.evaluate(time);
        cam.point_of_interest_enabled  = point_of_interest_enabled;

        // Populate physical lens model from animated fields.
        cam.lens.focal_length   = focal_length.evaluate(time);
        cam.lens.sensor_width   = sensor_width.evaluate(time);
        cam.lens.sensor_height  = sensor_height.evaluate(time);
        cam.lens.f_stop         = f_stop.evaluate(time);
        cam.lens.close_focus    = close_focus.evaluate(time);
        cam.lens.gate_fit       = gate_fit;

        // DoF: enabled explicitly OR via any animated param (legacy or physical).
        cam.dof.enabled  = dof_enabled || focus_z.is_animated() || aperture.is_animated() || max_blur.is_animated()
                           || focal_length.is_animated() || f_stop.is_animated() || focus_distance.is_animated()
                           || sensor_width.is_animated();
        cam.dof.focus_z  = focus_z.evaluate(time);
        cam.dof.aperture = aperture.evaluate(time);
        cam.dof.max_blur = max_blur.evaluate(time);

        // Focus distance (runtime-evaluated, stays in dof).
        cam.dof.focus_distance      = focus_distance.evaluate(time);
        cam.dof.use_physical_model  = use_physical_model;

        return cam;
    }

    /// Return true if any property has keyframes or expressions beyond its default.
    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || orientation.is_animated() || rotation.is_animated() ||
               rotation_offset_euler.is_animated() ||
               zoom.is_animated()     || fov_deg.is_animated() ||
               point_of_interest.is_animated() ||
               focus_z.is_animated()  || aperture.is_animated() || max_blur.is_animated() ||
               focal_length.is_animated() || sensor_width.is_animated() || sensor_height.is_animated() ||
               f_stop.is_animated()   || focus_distance.is_animated() || close_focus.is_animated();
    }

    /// Return true if any property depends on time (keyframes or expression).
    /// Includes physical lens parameters that affect the visual result
    /// (focal_length, sensor_width, f_stop, focus_distance).
    [[nodiscard]] bool is_time_dependent() const {
        return position.is_time_dependent() || orientation.is_time_dependent() ||
               rotation.is_time_dependent() || rotation_offset_euler.is_time_dependent() ||
               zoom.is_time_dependent()     || fov_deg.is_time_dependent() ||
               point_of_interest.is_time_dependent() ||
               focus_z.is_time_dependent()  || aperture.is_time_dependent() || max_blur.is_time_dependent() ||
               focal_length.is_time_dependent() || sensor_width.is_time_dependent() || sensor_height.is_time_dependent() ||
               f_stop.is_time_dependent()   || focus_distance.is_time_dependent() || close_focus.is_time_dependent();
    }
};

} // namespace chronon3d
