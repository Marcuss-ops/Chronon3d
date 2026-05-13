#include <chronon3d/chronon3d.hpp>
#include <string>
#include <cmath>

using namespace chronon3d;

static Composition TodayFirst25D() {
    return composition({
        .name = "TodayFirst25D",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = static_cast<f32>(ctx.frame) / 119.0f;

        s.camera_2_5d({
            .enabled = true,
            .position = {
                interpolate(t, 0.0f, 1.0f, -120.0f, 120.0f),
                interpolate(t, 0.0f, 1.0f, 30.0f, -30.0f),
                interpolate(t, 0.0f, 1.0f, -1200.0f, -900.0f)
            },
            .zoom = 1000.0f,
            .dof = {
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.010f,
                .max_blur = 14.0f
            }
        });

        // Background sky
        s.rect("sky", {
            .size = {1600, 900},
            .color = Color{0.03f, 0.05f, 0.12f, 1.0f},
            .pos = {0, 0, 0}
        });

        // Far stars/grid
        s.layer("far_grid", [](LayerBuilder& l) {
            l.enable_3d()
             .depth_role(DepthRole::FarBackground)
             .opacity(0.35f);

            for (int i = -5; i <= 5; ++i) {
                l.rect("v" + std::to_string(i), {
                    .size = {2, 1200},
                    .color = Color{0.2f, 0.4f, 1.0f, 0.5f},
                    .pos = {static_cast<f32>(i * 220), 0, 0}
                });
            }
        });

        // Background panels
        s.layer("background_panels", [](LayerBuilder& l) {
            l.enable_3d()
             .depth_role(DepthRole::Background);

            l.rounded_rect("left_panel", {
                .size = {500, 280},
                .radius = 30,
                .color = Color{0.06f, 0.10f, 0.25f, 0.9f},
                .pos = {-360, -80, 0}
            });

            l.rounded_rect("right_panel", {
                .size = {500, 280},
                .radius = 30,
                .color = Color{0.06f, 0.10f, 0.25f, 0.9f},
                .pos = {360, 80, 0}
            });
        });

        // Subject card
        s.layer("hero_card", [&](LayerBuilder& l) {
            l.enable_3d()
             .depth_role(DepthRole::Subject);

            const f32 bob = std::sin(static_cast<f32>(ctx.frame) * 0.06f) * 10.0f;

            l.rounded_rect("card_shadow", {
                .size = {620, 340},
                .radius = 44,
                .color = Color{0.0f, 0.0f, 0.0f, 0.35f},
                .pos = {10, bob + 16, 0}
            });

            l.rounded_rect("card", {
                .size = {620, 340},
                .radius = 44,
                .color = Color{0.12f, 0.18f, 0.42f, 1.0f},
                .pos = {0, bob, 0}
            });

            TextStyle title{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 56.0f,
                .color = Color::white()
            };

            l.text("title", {
                .content = "FIRST 2.5D SHOT",
                .style = title,
                .pos = {-250, bob - 35, 0}
            });

            TextStyle sub{
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .size = 24.0f,
                .color = Color{0.75f, 0.85f, 1.0f, 1.0f}
            };

            l.text("sub", {
                .content = "RenderGraph camera parallax",
                .style = sub,
                .pos = {-210, bob + 45, 0}
            });
        });

        // Foreground bars/particles
        s.layer("foreground_frame", [](LayerBuilder& l) {
            l.enable_3d()
             .depth_role(DepthRole::Foreground)
             .opacity(0.75f);

            l.rect("left_fg", {
                .size = {180, 900},
                .color = Color{0.0f, 0.0f, 0.0f, 0.85f},
                .pos = {-650, 0, 0}
            });

            l.rect("right_fg", {
                .size = {180, 900},
                .color = Color{0.0f, 0.0f, 0.0f, 0.85f},
                .pos = {650, 0, 0}
            });

            for (int i = 0; i < 12; ++i) {
                l.circle("p" + std::to_string(i), {
                    .radius = 8.0f + static_cast<f32>(i % 4) * 4.0f,
                    .color = Color{0.5f, 0.7f, 1.0f, 0.35f},
                    .pos = {
                        static_cast<f32>((i * 193) % 1200 - 600),
                        static_cast<f32>((i * 317) % 700 - 350),
                        0
                    }
                });
            }
        });

        // 2D overlay
        s.layer("hud_2d", [&](LayerBuilder& l) {
            TextStyle hud{
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .size = 16.0f,
                .color = Color{0.7f, 0.8f, 1.0f, 0.8f}
            };

            l.text("hud_left", {
                .content = "2.5D GRAPH TEST",
                .style = hud,
                .pos = {-600, -330, 0}
            });

            l.text("hud_right", {
                .content = "FRAME " + std::to_string(ctx.frame),
                .style = hud,
                .pos = {470, -330, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TodayFirst25D", TodayFirst25D)
