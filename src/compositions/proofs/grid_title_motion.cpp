// GridTitleMotion
//   chronon3d_cli video GridTitleMotion --graph --start 0 --end 72 --fps 24 -o output/proofs/grid_title_motion_3s.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <cmath>

using namespace chronon3d;

namespace {

static Composition grid_title_motion() {
    return composition({
        .name = "GridTitleMotion",
        .width = 1920,
        .height = 1080,
        .duration = 72
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float phase = (t * 3.14159265f) - 1.57079633f;
        const float cam_x = std::sin(phase) * 120.0f;
        const float cam_y = std::cos(phase) * 18.0f;

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {cam_x, cam_y, -1000.0f};
        cam.zoom = 1000.0f;
        cam.point_of_interest = {0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);

        DarkGridBgParams bg;
        bg.centered = true;
        bg.spacing = 120.0f;
        bg.bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f};
        bg.grid_color = Color{1.0f, 1.0f, 1.0f, 0.08f};
        scene::utils::dark_grid_background(s, ctx, bg);

        s.layer("title", [](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, 0.0f, 0.0f});
            l.opacity(0.84f);
            l.text("title", TextParams{
                .text = "TEXT IN MOTION",
                .size = {880.0f, 120.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 78.0f,
                .color = Color{0.98f, 0.98f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .line_height = 1.0f,
                .tracking = 1.0f,
            });
            l.with_glow(Glow{
                .radius = 32.0f,
                .intensity = 0.9f,
                .color = Color{0.98f, 0.98f, 1.0f, 0.95f}
            });
        });

        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("GridTitleMotion", grid_title_motion)
