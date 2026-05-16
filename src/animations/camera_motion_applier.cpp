#include <chronon3d/animations/camera_motion_applier.hpp>

#include <cmath>

namespace chronon3d::animation {

Camera2_5D make_camera_from_pose(const CameraMotionPose& pose) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = pose.position;
    cam.rotation = pose.rotation;
    cam.zoom = pose.zoom;
    return cam;
}

void apply_camera_motion(SceneBuilder& s,
                         const FrameContext& ctx,
                         const CameraMotionParams& p) {
    const Frame local_frame = (ctx.frame >= p.start_frame) ? (ctx.frame - p.start_frame) : 0;
    Camera2_5D cam = make_camera_from_pose(p.pose);

    if (p.primary.enabled && p.primary.duration > 0) {
        const f32 t = easing_value(p.primary.easing, normalized_time(local_frame, p.primary.duration));
        cam.position = lerp(p.primary.from.position, p.primary.to.position, t);
        cam.rotation = lerp(p.primary.from.rotation, p.primary.to.rotation, t);
        cam.zoom = lerp(p.primary.from.zoom, p.primary.to.zoom, t);
    } else {
        const f32 t = normalized_time(local_frame, p.duration);
        cam.position = p.position;
        cam.zoom = p.zoom;
        switch (p.axis) {
        case MotionAxis::Tilt:
            cam.rotation.x = lerp(p.start_deg, p.end_deg, t);
            break;
        case MotionAxis::Pan:
            cam.rotation.y = lerp(p.start_deg, p.end_deg, t);
            break;
        case MotionAxis::Roll:
            cam.rotation.z = lerp(p.start_deg, p.end_deg, t);
            break;
        }
    }

    if (p.idle.enabled) {
        const f32 phase = 2.0f * 3.1415926535f * p.idle.frequency_hz * ctx.seconds() + p.idle.phase_offset;
        const f32 wave = std::sin(phase);
        const Camera2_5D idle_base = p.idle.base_on_final
            ? cam
            : make_camera_from_pose(p.primary.enabled ? p.primary.from : p.pose);
        cam.position = idle_base.position + p.idle.position_amplitude * wave;
        cam.rotation = idle_base.rotation + p.idle.rotation_amplitude_deg * wave;
        cam.zoom = idle_base.zoom + p.idle.zoom_amplitude * wave;
    }

    s.camera().set(cam);
}

} // namespace chronon3d::animation
