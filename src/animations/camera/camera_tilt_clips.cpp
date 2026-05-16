#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {
namespace {

using chronon3d::animation::CameraMotionParams;
using chronon3d::animation::MotionAxis;

void build_reference_image_content(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
    const std::string reference_image = p.reference_image;

    const f32 inset_x = static_cast<f32>(ctx.width) * 0.06f;
    const f32 inset_y = static_cast<f32>(ctx.height) * 0.06f;
    const Vec2 image_size{
        static_cast<f32>(ctx.width) - inset_x * 2.0f,
        static_cast<f32>(ctx.height) - inset_y * 2.0f,
    };
    const Vec3 image_pos{
        static_cast<f32>(ctx.width) * 0.5f,
        static_cast<f32>(ctx.height) * 0.5f,
        0.0f,
    };

    scene::utils::dark_grid_background(s, ctx);

    s.layer("reference-image", [reference_image, image_size, image_pos](LayerBuilder& l) {
        l.enable_3d()
         .image("grid_reference", {
             .path = reference_image,
             .size = image_size,
             .pos = image_pos,
             .opacity = 1.0f,
         });
    });
}

Composition make_camera_image_clip() {
    CameraMotionParams params;
    params.axis = MotionAxis::Tilt;
    params.duration = 60;
    params.start_frame = 0;

    return chronon3d::presets::camera_motion_clip(
        "CameraImageClip",
        params,
        [](SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
            build_reference_image_content(s, ctx, p);
        });
}

CHRONON_REGISTER_COMPOSITION("CameraImageClip", make_camera_image_clip)

} // namespace
} // namespace chronon3d
