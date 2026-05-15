#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <functional>
#include <utility>
#include <string>

namespace chronon3d::animation {

enum class MotionAxis {
    Tilt,
    Pan,
    Roll,
};

struct CameraMotionParams {
    MotionAxis axis{MotionAxis::Tilt};
    f32 start_deg{-18.0f};
    f32 end_deg{18.0f};
    Frame duration{60};
    Frame start_frame{0};
    Vec3 position{0.0f, 0.0f, -1080.0f};
    f32 zoom{1080.0f};
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

inline void apply_camera_motion(SceneBuilder& s,
                                const FrameContext& ctx,
                                const CameraMotionParams& p) {
    s.enable_camera_2_5d(true)
     .camera_position(p.position)
     .camera_zoom(p.zoom);

    const Frame local_frame = (ctx.frame >= p.start_frame) ? (ctx.frame - p.start_frame) : 0;
    const f32 t = normalized_time(local_frame, p.duration);
    switch (p.axis) {
    case MotionAxis::Tilt:
        s.camera_tilt(lerp(p.start_deg, p.end_deg, t));
        break;
    case MotionAxis::Pan:
        s.camera_pan(lerp(p.start_deg, p.end_deg, t));
        break;
    case MotionAxis::Roll:
        s.camera_roll(lerp(p.start_deg, p.end_deg, t));
        break;
    }
}

inline Composition camera_motion_clip(std::string name,
                                      CameraMotionParams params,
                                      ContentBuilder content_builder) {
    return composition({
        .name = std::move(name),
        .width = 1920,
        .height = 1080,
        .duration = params.duration,
    }, [params = std::move(params), content_builder = std::move(content_builder)](
           const FrameContext& ctx) {
        SceneBuilder s(ctx);
        apply_camera_motion(s, ctx, params);
        content_builder(s, ctx, params);
        return s.build();
    });
}

} // namespace chronon3d::animation
