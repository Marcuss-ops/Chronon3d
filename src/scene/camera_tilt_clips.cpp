#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/assets/asset_ref.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {
namespace {

using chronon3d::animation::CameraMotionParams;
using chronon3d::animation::MotionAxis;

void build_reference_image_content(SceneBuilder& s, const FrameContext& ctx, const CameraMotionParams& p) {
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

    // No implicit engine default image: the core no longer embeds the
    // repository-relative `assets/images/camera_reference.jpg`. The
    // reference layer exists only when the caller supplied an explicit
    // ImageRef (resolved through the canonical per-runtime AssetResolver).
    if (!p.reference_image.has_value()) {
        return;
    }
    const assets::ImageRef reference_image = *p.reference_image;

    s.layer("reference-image", [reference_image, image_size, image_pos](LayerBuilder& l) {
        l.enable_3d()
         .image("grid_reference", {
             .source = reference_image,
             .size = image_size,
             .pos = image_pos,
             .opacity = 1.0f,
         });
    });
}

} // namespace
} // namespace chronon3d

namespace chronon3d {

void register_camera_tilt_clip(CompositionRegistry& registry) {
    registry.add(make_composition_descriptor("CameraImageClip", [](const CompositionProps&) {
        CameraMotionParams params;
        params.axis = animation::MotionAxis::Tilt;
        params.duration = 60;
        params.start_frame = 0;

        return presets::camera_motion_clip(
            "CameraImageClip",
            params,
            [](SceneBuilder& s, const FrameContext& ctx, const animation::CameraMotionParams& p) {
                build_reference_image_content(s, ctx, p);
            });
    }));
}

} // namespace chronon3d
