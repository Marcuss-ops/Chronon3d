// CameraMotionParams authoring adapter for the canonical Camera V1 descriptor.
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/animations/camera_motion_params.hpp>

namespace chronon3d::camera_v1 {

Camera2_5D CameraMotionParamsSource::sample_at(Frame ctx_frame) const {
    using namespace chronon3d::animation;

    const Frame local_frame = (ctx_frame >= params.start_frame)
                                  ? (ctx_frame - params.start_frame)
                                  : Frame{0};
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = params.pose.position;
    cam.rotation = params.pose.rotation;
    cam.zoom = params.pose.zoom;

    if (params.primary.enabled && params.primary.duration > 0) {
        const f32 t = easing_value(
            params.primary.easing,
            normalized_time(local_frame, params.primary.duration));
        cam.position = lerp(params.primary.from.position, params.primary.to.position, t);
        cam.rotation = lerp(params.primary.from.rotation, params.primary.to.rotation, t);
        cam.zoom = lerp(params.primary.from.zoom, params.primary.to.zoom, t);
    } else {
        const f32 t = normalized_time(local_frame, params.duration);
        cam.position = params.position;
        cam.zoom = params.zoom;
        switch (params.axis) {
            case MotionAxis::Tilt: cam.rotation.x = lerp(params.start_deg, params.end_deg, t); break;
            case MotionAxis::Pan:  cam.rotation.y = lerp(params.start_deg, params.end_deg, t); break;
            case MotionAxis::Roll: cam.rotation.z = lerp(params.start_deg, params.end_deg, t); break;
        }
    }
    return cam;
}

CameraDescriptor camera_descriptor_from(
    const chronon3d::animation::CameraMotionParams& p) {
    CameraDescriptor d;
    d.id = "camera_motion_params";
    if (p.idle.enabled) {
        IdleOscillation idle;
        idle.position_amplitude = p.idle.position_amplitude;
        idle.rotation_amplitude_deg = p.idle.rotation_amplitude_deg;
        idle.zoom_amplitude = p.idle.zoom_amplitude;
        idle.frequency_hz = p.idle.frequency_hz;
        idle.phase = p.idle.phase_offset;
        d.modifiers.push_back(idle);
    }
    d.source = CameraMotionParamsSource{p};
    d.base.position = p.primary.enabled ? p.primary.from.position : p.position;
    d.base.rotation = p.pose.rotation;
    d.base.projection = ZoomProjection{AnimatedValue<float>{p.zoom}};
    d.base.lens.sensor_width = 36.0f;
    d.base.lens.sensor_height = 24.0f;
    d.base.lens.focal_length = p.pose.zoom;
    d.base.lens.f_stop = 2.8f;
    d.base.lens.gate_fit = GateFit::Fill;
    return d;
}

} // namespace chronon3d::camera_v1
