// ── CatmullRomCameraMotion — drives Camera2_5D along Catmull-Rom path ──────
// Extracted from catmull_rom_path.hpp (FASE 13).
//
// Drop-in complement to CameraMotionPath. Use this when you want the camera to
// pass *through* each waypoint exactly (interpolating spline), as opposed to
// the Bezier version where you tune handles and the curve only approximates.

#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d {

struct CatmullRomCameraMotion {
    CatmullRomPath3D path;
    AutoOrientMode   auto_orient{AutoOrientMode::None};
    Vec3             point_of_interest{0.0f, 0.0f, 0.0f};
    EasingCurve      easing{Easing::Linear};
    f32              zoom{1000.0f};
    f32              fov_deg{50.0f};
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};
    f32              roll_deg{0.0f};
    bool             use_arc_length{false};

    [[nodiscard]] Camera2_5D evaluate(Frame frame, Frame start, Frame end) const {
        Camera2_5D cam;
        cam.enabled = true;

        if (path.empty() || end <= start) {
            cam.position = path.empty() ? Vec3{0.0f, 0.0f, -1000.0f} : path.evaluate(0.0f);
            cam.zoom = zoom;
            cam.fov_deg = fov_deg;
            return cam;
        }

        const f32 raw_t = static_cast<f32>(frame - start) / static_cast<f32>(end - start);
        const f32 t = std::clamp(easing.apply(std::clamp(raw_t, 0.0f, 1.0f)), 0.0f, 1.0f);

        const PathSample sample = path.sample_at(t, use_arc_length);
        cam.position = sample.position;
        cam.zoom = zoom;
        cam.fov_deg = fov_deg;

        switch (auto_orient) {
            case AutoOrientMode::None:
                break;
            case AutoOrientMode::AlongPath: {
                Vec3 forward = glm::normalize(sample.forward);
                if (glm::length(forward) > 1e-4f) {
                    const Quat orientation = quat_look_along(forward);
                    cam.rotation = quat_to_camera_euler(orientation, roll_deg);
                    cam.point_of_interest = cam.position + forward * 1000.0f;
                    cam.point_of_interest_enabled = true;
                }
                break;
            }
            case AutoOrientMode::TowardsPOI: {
                cam.point_of_interest = point_of_interest;
                cam.point_of_interest_enabled = true;
                const Vec3 look_dir = glm::normalize(point_of_interest - cam.position);
                const Quat orientation = quat_look_along(look_dir);
                cam.rotation = quat_to_camera_euler(orientation, roll_deg);
                break;
            }
        }
        return cam;
    }

    [[nodiscard]] Camera2_5D evaluate(f32 t) const {
        Camera2_5D cam;
        cam.enabled = true;
        if (path.empty()) {
            cam.position = Vec3{0.0f, 0.0f, -1000.0f};
            cam.zoom = zoom;
            cam.fov_deg = fov_deg;
            return cam;
        }
        t = std::clamp(t, 0.0f, 1.0f);

        const PathSample sample = path.sample_at(t, use_arc_length);
        cam.position = sample.position;
        cam.zoom = zoom;
        cam.fov_deg = fov_deg;

        switch (auto_orient) {
            case AutoOrientMode::None: break;
            case AutoOrientMode::AlongPath: {
                Vec3 forward = glm::normalize(sample.forward);
                if (glm::length(forward) > 1e-4f) {
                    const Quat orientation = quat_look_along(forward);
                    cam.rotation = quat_to_camera_euler(orientation, roll_deg);
                    cam.point_of_interest = cam.position + forward * 1000.0f;
                    cam.point_of_interest_enabled = true;
                }
                break;
            }
            case AutoOrientMode::TowardsPOI: {
                cam.point_of_interest = point_of_interest;
                cam.point_of_interest_enabled = true;
                const Vec3 look_dir = glm::normalize(point_of_interest - cam.position);
                const Quat orientation = quat_look_along(look_dir);
                cam.rotation = quat_to_camera_euler(orientation, roll_deg);
                break;
            }
        }
        return cam;
    }
};

} // namespace chronon3d
