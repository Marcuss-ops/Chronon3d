#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
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
struct AnimatedCamera2_5D {
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

    /// Evaluate all animated properties at `frame` and return a static Camera2_5D.
    [[nodiscard]] Camera2_5D evaluate(Frame frame) const {
        Camera2_5D cam;
        cam.enabled = enabled;
        cam.position = position.evaluate(frame);
        cam.rotation = rotation.evaluate(frame);
        cam.zoom     = zoom.evaluate(frame);
        cam.fov_deg  = fov_deg.evaluate(frame);

        cam.point_of_interest          = point_of_interest.evaluate(frame);
        cam.point_of_interest_enabled  = point_of_interest_enabled;

        cam.dof.enabled  = focus_z.is_animated() || aperture.is_animated() || max_blur.is_animated();
        cam.dof.focus_z  = focus_z.evaluate(frame);
        cam.dof.aperture = aperture.evaluate(frame);
        cam.dof.max_blur = max_blur.evaluate(frame);

        return cam;
    }

    /// Return true if any property has keyframes or expressions beyond its default.
    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || rotation.is_animated() ||
               zoom.is_animated()     || fov_deg.is_animated() ||
               point_of_interest.is_animated() ||
               focus_z.is_animated()  || aperture.is_animated() || max_blur.is_animated();
    }
};

} // namespace chronon3d
