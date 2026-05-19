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
        
        // 1. Dynamic parallax sweep camera
        s.camera().set(camera_motion::parallax_sweep(t, 24.0f, -1000.0f, 1000.0f));

        // 2. High-end dark grid background
        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 84.0f,
            .extent = 4000.0f,
            .centered = true
        });

        // 3. Dynamic bob & sway calculations
        const float bob = std::sin(t * 6.2831853f) * 6.0f;
        const float sway = std::sin(t * 3.1415926f * 1.35f) * 8.0f;

        // 4. Create typography object with PushIn3D transition and highly visible rotation sway
        std::vector<presets::motion::MotionObject> objects = {
            presets::motion::MotionObject::text("title", "LIL DIRK")
                .at({0.0f, 18.0f + bob, -120.0f})
                .preset(presets::motion::MotionPreset::PushIn3D)
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
                .rotate_3d({-2.0f + bob * 0.12f, sway * 1.5f, 0.0f}),
        };

        presets::motion::draw_motion_objects(s, ctx, objects);

        return s.build();
    });
}

static Composition lil_dirk_clean_fast() {
    return composition({
        .name     = "LilDirkCleanFast",
        .width    = 1920,
        .height   = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        
        // 1. Dynamic parallax sweep camera
        s.camera().set(camera_motion::parallax_sweep(t, 24.0f, -1000.0f, 1000.0f));

        // 2. High-end dark grid background (Static cached!)
        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 84.0f,
            .extent = 4000.0f,
            .centered = true
        });

        // 3. Dynamic bob & sway calculations
        const float bob = std::sin(t * 6.2831853f) * 6.0f;
        const float sway = std::sin(t * 3.1415926f * 1.35f) * 8.0f;

        // 4. Create typography object without costly multi-pass CPU glow
        std::vector<presets::motion::MotionObject> objects = {
            presets::motion::MotionObject::text("title", "LIL DIRK")
                .at({0.0f, 18.0f + bob, -120.0f})
                .preset(presets::motion::MotionPreset::PushIn3D)
                .time(0, 120)
                .font_path("assets/fonts/Inter-Bold.ttf")
                .font_family("Inter")
                .font_weight(800)
                .font_size(112.0f)
                .tracking(1.0f)
                .align(presets::motion::TextAlign::Center)
                .color(Color{0.98f, 0.98f, 0.96f, 1.0f})
                .glow(false) // Optimized: Disabled heavy 5-pass glow!
                .enable_3d()
                .rotate_3d({-2.0f + bob * 0.12f, sway * 1.5f, 0.0f}),
        };

        presets::motion::draw_motion_objects(s, ctx, objects);

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
CHRONON_REGISTER_COMPOSITION("LilDirkCleanFast", lil_dirk_clean_fast)

