#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

/// AnimatedCamera2_5D — a Camera2_5D where every key property is animatable
/// via keyframed AnimatedValue<T> fields.
///
/// Evaluate with .evaluate(frame) to produce a static Camera2_5D at any frame.
///
/// Usage:
///   AnimatedCamera2_5D cam;
///   cam.position
///       .key(0,   Vec3{0,0,-1200})
///       .key(90,  Vec3{0,0,-800},  EasingCurve{Easing::OutCubic});
///   cam.fov_deg.set(50.0f);
///   Camera2_5D static_cam = cam.evaluate(45);
///
/// @deprecated Use CameraDescriptor + compile_camera() + CameraProgram instead.
/// Migration path: convert to PoseTracksSource via camera_descriptor_from()
/// adapter in `<chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>`.
struct [[deprecated("Use CameraDescriptor + compile_camera() + CameraProgram instead. "
                     "See camera_descriptor_from() adapter.")]] AnimatedCamera2_5D {
    bool enabled{true};

    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, -1000.0f}};
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
        cam.rotation = rotation.evaluate(time);
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
        return position.is_animated() || rotation.is_animated() ||
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
        return position.is_time_dependent() || rotation.is_time_dependent() ||
               zoom.is_time_dependent()     || fov_deg.is_time_dependent() ||
               point_of_interest.is_time_dependent() ||
               focus_z.is_time_dependent()  || aperture.is_time_dependent() || max_blur.is_time_dependent() ||
               focal_length.is_time_dependent() || sensor_width.is_time_dependent() || sensor_height.is_time_dependent() ||
               f_stop.is_time_dependent()   || focus_distance.is_time_dependent() || close_focus.is_time_dependent();
    }
};

} // namespace chronon3d
