#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <cmath>

using namespace chronon3d;

static Composition lil_dirk_clean() {
    return composition({
        .name     = "LilDirkClean",
        .width    = 1920,
        .height   = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {0.0f, 0.0f, -1000.0f};
        cam.zoom = 1000.0f;
        s.camera().set(cam);

        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 84.0f,
            .extent = 4000.0f,
            .centered = true
        });

        const float bob = std::sin(t * 6.2831853f) * 2.0f;
        const float sway = std::sin(t * 3.1415926f * 1.35f) * 2.5f;

        std::vector<presets::motion::MotionObject> objects = {
            presets::motion::MotionObject::text("title", "LIL DIRK")
                .at({0.0f, bob, -120.0f})
                .preset(presets::motion::MotionPreset::None)
                .time(0, 120)
                .font_path("assets/fonts/Inter-Bold.ttf")
                .font_family("Inter")
                .font_weight(800)
                .font_size(112.0f)
                .tracking(1.0f)
                .align(presets::motion::TextAlign::Center)
                .color(Color{0.98f, 0.98f, 0.96f, 1.0f})
                .glow(true)
                .enable_3d()
                .rotate_3d({-1.25f + bob * 0.03f, sway * 0.45f, 0.0f}),
        };

        presets::motion::draw_motion_objects(s, ctx, objects);

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
