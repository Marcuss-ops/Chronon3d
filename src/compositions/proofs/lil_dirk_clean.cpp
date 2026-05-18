#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

using namespace chronon3d;

static Composition lil_dirk_clean() {
    return composition({
        .name     = "LilDirkClean",
        .width    = 1920,
        .height   = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.05f, 0.05f, 0.055f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
            .spacing = 84.0f,
            .extent = 4000.0f
        });

        s.screen_layer("title", [](LayerBuilder& l) {

            l.text("title", {
                .content = "LIL DIRK",
                .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 112.0f,
                    .color = Color::white(),
                    .align = TextAlign::Center,
                },
                .pos = {0.0f, 22.0f, 0.0f},
            }).with_glow(Glow{
                .enabled = true,
                .radius = 18.0f,
                .intensity = 0.72f,
                .color = Color::white(),
            }).fake_3d_wave(Fake3DWaveParams{
                .amplitude_px = 10.0f,
                .frequency = 1.2f,
                .speed = 0.08f,
                .depth_px = 20.0f,
                .phase = 0.0f,
                .slices = 24,
                .axis = WaveAxis::Horizontal,
                .perspective = 0.06f,
                .highlight = 0.25f,
                .side_darkening = 0.18f,
                .shadow_enabled = false,
                .shadow_color = Color{1.0f, 0.05f, 0.05f, 0.80f},
                .shadow_offset = {10.0f, 8.0f},
                .shadow_blur = 0.0f,
                .expand_bounds = true,
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
