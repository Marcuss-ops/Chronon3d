// MotionObjectProof
//   chronon3d_cli render MotionObjectProof --graph --frames 0  -o output/proofs/motion_object_f000.png
//   chronon3d_cli render MotionObjectProof --graph --frames 15 -o output/proofs/motion_object_f015.png
//   chronon3d_cli render MotionObjectProof --graph --frames 30 -o output/proofs/motion_object_f030.png
//   chronon3d_cli render MotionObjectProof --graph --frames 60 -o output/proofs/motion_object_f060.png

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

static Composition motion_object_proof() {
    return composition({
        .name = "MotionObjectProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.05f, 0.05f, 0.06f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 72.0f,
            .extent = 3000.0f,
            .centered = true,
        });

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float bob = std::sin(t * 6.2831853f) * 6.0f;
        const float sway = std::sin(t * 3.1415926f * 1.2f) * 5.0f;

        std::vector<MotionObject> objects = {
            MotionObject::rounded_rect("card")
                .at({0.0f, 10.0f, -40.0f})
                .size({860.0f, 470.0f})
                .color(Color{0.08f, 0.08f, 0.1f, 1.0f})
                .preset(MotionPreset::PopIn)
                .time(0, 90)
                .enable_3d(),

            MotionObject::text("title", "MOTION OBJECT")
                .at({0.0f, -150.0f + bob, -110.0f})
                .preset(MotionPreset::PushIn3D)
                .time(0, 90)
                .font_path("assets/fonts/Inter-Bold.ttf")
                .font_size(86.0f)
                .tracking(1.2f)
                .glow(true)
                .enable_3d()
                .rotate_3d({-2.0f, sway, 0.0f}),

            MotionObject::image("person", "assets/images/camera_reference.jpg")
                .at({-240.0f, 35.0f, -180.0f})
                .size({300.0f, 300.0f})
                .preset(MotionPreset::ParallaxDrift)
                .time(0, 90)
                .enable_3d()
                .parallax(1.5f),

            MotionObject::rounded_rect("badge")
                .at({250.0f, 160.0f, 10.0f})
                .size({180.0f, 52.0f})
                .color(Color{0.88f, 0.05f, 0.08f, 1.0f})
                .preset(MotionPreset::SlideLeft)
                .time(0, 90)
                .enable_3d(),

            MotionObject::text("badge_text", "NEW")
                .at({250.0f, 154.0f, 20.0f})
                .preset(MotionPreset::FadeIn)
                .time(0, 90)
                .font_path("assets/fonts/Inter-Bold.ttf")
                .font_size(30.0f)
                .color(Color::white())
                .enable_3d(),

            MotionObject::text("caption", "A single system for text, images and shapes")
                .at({110.0f, 170.0f, 0.0f})
                .preset(MotionPreset::FadeIn)
                .time(0, 90)
                .font_path("assets/fonts/Inter-Regular.ttf")
                .font_size(26.0f)
                .tracking(0.3f)
                .color(Color{0.82f, 0.84f, 0.88f, 1.0f}),
        };

        draw_motion_objects(s, ctx, objects);
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("MotionObjectProof", motion_object_proof)
