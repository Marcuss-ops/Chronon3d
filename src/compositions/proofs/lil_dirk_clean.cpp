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

        s.camera().set(camera_motion::parallax_sweep(t, 18.0f, -1000.0f, 1000.0f));

        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.05f, 0.05f, 0.055f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 84.0f,
            .extent = 4000.0f,
            .centered = true
        });

        const float bob = std::sin(t * 6.2831853f) * 5.0f;
        const float sway = std::sin(t * 3.1415926f * 1.35f) * 6.0f;

        presets::motion::soft_glow_text(s, "title", {
            .text = "LIL DIRK",
            .font_path_main = "assets/fonts/Inter-Bold.ttf",
            .font_path_glow = "assets/fonts/Inter-Regular.ttf",
            .motion = {
                .enabled = true,
                .position = {0.0f, 18.0f + bob, -120.0f},
                .rotation = {-2.0f + bob * 0.08f, sway, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
            .text_pos = {0.0f, 22.0f, 0.0f},
            .font_size = 112.0f,
            .outer_size_boost = 12.0f,
            .inner_size_boost = 5.0f,
            .outer_blur = 34.0f,
            .inner_blur = 16.0f,
            .outer_opacity = 0.14f,
            .inner_opacity = 0.30f,
            .tracking = 1.0f,
            .align = TextAlign::Center,
            .main_color = Color{0.98f, 0.98f, 0.96f, 1.0f},
            .glow_color = Color{1.0f, 1.0f, 1.0f, 1.0f},
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
