#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <utility>

namespace chronon3d::presets {

Composition camera_motion_clip(std::string name,
                               animation::CameraMotionParams params,
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
        animation::apply_camera_motion(s, ctx, params);
        content_builder(s, ctx, params);
        return s.build();
    });
}

} // namespace chronon3d::presets
