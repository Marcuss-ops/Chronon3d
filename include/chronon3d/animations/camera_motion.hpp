#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::animation {

enum class MotionAxis {
    Tilt,
    Pan,
    Roll,
};

struct CameraMotionPose {
    Vec3 position{0.0f, 0.0f, -1080.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    f32 zoom{1080.0f};
};

struct CameraMotionPrimary {
    CameraMotionPose from{};
    CameraMotionPose to{};
    Frame duration{0};
    std::string easing{"smoothstep"};
    bool enabled{false};
};

struct CameraMotionIdle {
    bool enabled{false};
    Vec3 position_amplitude{0.0f, 0.0f, 0.0f};
    Vec3 rotation_amplitude_deg{0.0f, 0.0f, 0.0f};
    f32 zoom_amplitude{0.0f};
    f32 frequency_hz{0.15f};
    f32 phase_offset{0.0f};
    bool base_on_final{true};
};

struct CameraMotionParams {
    MotionAxis axis{MotionAxis::Tilt};
    f32 start_deg{-18.0f};
    f32 end_deg{18.0f};
    Frame duration{60};
    Frame start_frame{0};
    int width{1920};
    int height{1080};
    Vec3 position{0.0f, 0.0f, -1080.0f};
    f32 zoom{1080.0f};
    CameraMotionPose pose{};
    CameraMotionPrimary primary{};
    CameraMotionIdle idle{};
    std::string reference_image{"assets/images/camera_reference.jpg"};
};

using ContentBuilder = std::function<void(SceneBuilder&, const FrameContext&, const CameraMotionParams&)>;

inline f32 lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

inline f32 smoothstep(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline f32 normalized_time(Frame frame, Frame duration) {
    const Frame span = std::max<Frame>(1, duration - 1);
    return smoothstep(static_cast<f32>(frame) / static_cast<f32>(span));
}

inline f32 easing_value(std::string_view easing, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (easing == "linear") {
        return t;
    }
    if (easing == "ease_out_cubic") {
        const f32 u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    if (easing == "ease_in_out_cubic") {
        return (t < 0.5f)
            ? 4.0f * t * t * t
            : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }
    return smoothstep(t);
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) {
    return a + (b - a) * t;
}

inline Camera2_5D make_camera_from_pose(const CameraMotionPose& pose) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = pose.position;
    cam.rotation = pose.rotation;
    cam.zoom = pose.zoom;
    return cam;
}

inline void apply_camera_motion(SceneBuilder& s,
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

    s.camera_2_5d(cam);
}

inline Composition camera_motion_clip(std::string name,
                                      CameraMotionParams params,
                                      ContentBuilder content_builder) {
    const Frame total_duration = params.primary.enabled
        ? std::max(params.duration, params.primary.duration)
        : params.duration;
    return composition({
        .name = std::move(name),
        .width = params.width,
        .height = params.height,
        .duration = total_duration,
    }, [params = std::move(params), content_builder = std::move(content_builder)](
           const FrameContext& ctx) {
        SceneBuilder s(ctx);
        apply_camera_motion(s, ctx, params);
        content_builder(s, ctx, params);
        return s.build();
    });
}

} // namespace chronon3d::animation
