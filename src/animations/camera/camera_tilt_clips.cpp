#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/core/composition_registration.hpp>

namespace chronon3d {
namespace {

using chronon3d::animation::CameraMotionParams;
using chronon3d::animation::ContentBuilder;
using chronon3d::animation::MotionAxis;

void build_reference_image_content(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
    const char* reference_image = p.reference_image;
    s.layer("reference-image", [ctx, reference_image](LayerBuilder& l) {
        l.enable_3d()
         .image("grid_reference", {
             .path = reference_image,
             .size = {static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)},
             .pos = {static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f},
             .opacity = 1.0f,
         });
    });
}

Composition make_camera_tilt_image_tilt_clip() {
    return animation::camera_motion_clip(
        "CameraTiltImageTiltClip",
        MotionAxis::Tilt,
        {},
        [](SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
            build_reference_image_content(s, ctx, p);
        });
}

Composition make_camera_tilt_image_pan_clip() {
    return animation::camera_motion_clip(
        "CameraTiltImagePanClip",
        MotionAxis::Pan,
        {},
        [](SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
            build_reference_image_content(s, ctx, p);
        });
}

Composition make_camera_tilt_image_roll_clip() {
    return animation::camera_motion_clip(
        "CameraTiltImageRollClip",
        MotionAxis::Roll,
        {},
        [](SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
            build_reference_image_content(s, ctx, p);
        });
}

CHRONON_REGISTER_COMPOSITION("CameraTiltImageTiltClip", make_camera_tilt_image_tilt_clip)
CHRONON_REGISTER_COMPOSITION("CameraTiltImagePanClip", make_camera_tilt_image_pan_clip)
CHRONON_REGISTER_COMPOSITION("CameraTiltImageRollClip", make_camera_tilt_image_roll_clip)

} // namespace
} // namespace chronon3d
