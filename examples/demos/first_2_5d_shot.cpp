// first_2_5d_shot.cpp
#include <string>
// ─────────────────────────────────────────────────────────────────────────
// Cinematic 2.5D composition: 4 depth planes, animated camera pan + push-in.
// Demonstrates the modular graph path (--graph flag) with:
//   - DepthRole semantic Z placement
//   - Parallax from camera X movement
//   - Zoom push-in (camera moves forward on Z)
//   - Depth-of-field blur at extreme depths
//   - 2D HUD overlay unaffected by camera
//
// Render with:
//   chronon3d_cli --graph --comp First2_5DShot --out output/shot_%04d.png
//   chronon3d_cli --graph --comp First2_5DShot --out output/shot.mp4 --fps 30
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;
Composition First2_5DShot() {
    return composition({
        .name     = "First2_5DShot",
        .width    = 1920,
        .height   = 1080,
        .duration = 180
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = static_cast<f32>(ctx.frame) / 179.0f;
        
        // ── Camera: Dynamic pan + Push-in ─────────────────────────────────
        const f32 cam_x = interpolate(t, 0.0f, 1.0f, -60.0f, 60.0f);
        const f32 cam_y = interpolate(t, 0.0f, 1.0f, 30.0f, -30.0f);
        const f32 cam_z = interpolate(t, 0.0f, 1.0f, -1200.0f, -800.0f);

        s.camera_2_5d({
            .enabled  = true,
            .position = {cam_x, cam_y, cam_z},
            .zoom     = 1000.0f,
            .dof = {
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.015f,
                .max_blur = 20.0f
            }
        });

        // ── Background: Deep space / Vignette ─────────────────────────────
        s.rect("sky", {
            .size  = {2000, 1100}, // Slightly larger to cover any motion
            .color = Color{0.02f, 0.02f, 0.05f, 1.0f},
            .pos   = {0, 0, 0}
        });

        // ── Far Background: Distant Grid ──────────────────────────────────
        s.layer("grid", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::FarBackground).opacity(0.3f);
            
            for (int i = -4; i <= 4; ++i) {
                l.rect("v-line-" + std::to_string(i), {
                    .size = {2, 4000},
                    .color = Color{0.2f, 0.4f, 1.0f, 0.5f},
                    .pos = {static_cast<f32>(i * 600), 0, 0}
                });
                l.rect("h-line-" + std::to_string(i), {
                    .size = {4000, 2},
                    .color = Color{0.2f, 0.4f, 1.0f, 0.5f},
                    .pos = {0, static_cast<f32>(i * 600), 0}
                });
            }
        });

        // ── Midground: Floating Panels ────────────────────────────────────
        s.layer("mid-panels", [&](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Background);

            const f32 angle = std::sin(static_cast<f32>(ctx.frame) * 0.03f) * 2.0f;

            l.rounded_rect("panel-l", {
                .size   = {800, 500},
                .radius = 40,
                .color  = Color{0.05f, 0.10f, 0.25f, 0.8f},
                .pos    = {-600, -100, 0}
            }).rotate_node({0, 0, angle});

            l.rounded_rect("panel-r", {
                .size   = {800, 500},
                .radius = 40,
                .color  = Color{0.05f, 0.10f, 0.25f, 0.8f},
                .pos    = {600, 100, 0}
            }).rotate_node({0, 0, -angle});
        });

        // ── Subject: Hero Card ────────────────────────────────────────────
        s.layer("hero", [&](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Subject);

            const f32 float_y = std::sin(static_cast<f32>(ctx.frame) * 0.05f) * 15.0f;
            const f32 rot     = std::cos(static_cast<f32>(ctx.frame) * 0.04f) * 1.5f;

            // Glassmorphism-style card
            l.rounded_rect("card-border", {
                .size   = {806, 506},
                .radius = 64,
                .color  = Color{0.4f, 0.6f, 1.0f, 1.0f}, // Neon border
                .pos    = {0, float_y, 0}
            }).rotate_node({0, 0, rot});

            l.rounded_rect("card-body", {
                .size   = {800, 500},
                .radius = 60,
                .color  = Color{0.08f, 0.12f, 0.28f, 0.95f},
                .pos    = {0, float_y, 0}
            }).rotate_node({0, 0, rot});

            // Title with GLOW
            TextStyle title_style{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size      = 72.0f,
                .color     = Color::white()
            };
            
            l.text("title", {
                .content = "CHRONON 3D",
                .style   = title_style,
                .pos     = {-230, float_y - 20, 0}
            }).rotate_node({0, 0, rot})
              .with_glow({.enabled = true, .radius = 20.0f, .intensity = 1.2f, .color = Color{0.3f, 0.6f, 1.0f, 1.0f}});

            TextStyle sub_style{
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .size      = 24.0f,
                .color     = Color{0.7f, 0.8f, 1.0f, 0.8f}
            };
            l.text("subtitle", {
                .content = "ULTRA-FLUID 2.5D ENGINE",
                .style   = sub_style,
                .pos     = {-150, float_y + 60, 0}
            }).rotate_node({0, 0, rot});
        });

        // ── Foreground: Particles & Bokeh ─────────────────────────────────
        s.layer("foreground-particles", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Foreground).opacity(0.8f);

            for (int i = 0; i < 20; ++i) {
                f32 px = (static_cast<f32>(i * 2345 % 4000) - 2000);
                f32 py = (static_cast<f32>(i * 6789 % 2000) - 1000);
                l.circle("p-" + std::to_string(i), {
                    .radius = 10.0f + (i % 5) * 5.0f,
                    .color  = Color{0.6f, 0.8f, 1.0f, 0.4f},
                    .pos    = {px, py, 0}
                });
            }
        });

        // ── 2D HUD ────────────────────────────────────────────────────────
        s.layer("hud", [&](LayerBuilder& l) {
            TextStyle ts{.font_path = "assets/fonts/Inter-Regular.ttf", .size = 16.0f, .color = Color{0.5f, 0.5f, 0.6f, 1.0f}};
            l.text("id", {
                .content = "P-772 RENDER_GRAPH_V1.1",
                .style   = ts,
                .pos     = {-920, -500, 0}
            });
            l.text("frame", {
                .content = "F_" + std::to_string(ctx.frame),
                .style   = ts,
                .pos     = {830, -500, 0}
            });
            
            // Subtle scanlines / vignette overlay
            l.rect("vignette", {
                .size = {1920, 1080},
                .color = Color{0, 0, 0, 0.15f},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("First2_5DShot", First2_5DShot)

// ─────────────────────────────────────────────────────────────────────────
// First2_5DShotFov — same scene but using FOV projection instead of zoom.
// 35° ≈ telephoto (compressed depth), 70° ≈ wide angle (exaggerated parallax).
// ─────────────────────────────────────────────────────────────────────────
Composition First2_5DShotFov() {
    return composition({
        .name     = "First2_5DShotFov",
        .width    = 1920,
        .height   = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t       = static_cast<f32>(ctx.frame) / 89.0f;
        const f32 cam_x   = interpolate(t, 0.0f, 1.0f,   0.0f, -100.0f);  // pan left
        const f32 fov     = interpolate(t, 0.0f, 1.0f,  35.0f,   65.0f);  // widening FOV

        s.enable_camera_2_5d()
         .camera_position({cam_x, 0.0f, -1000.0f})
         .camera_fov(fov);   // uses projection_mode::Fov automatically

        // Background
        s.rect("bg", {
            .size  = {2000, 1100},
            .color = Color{0.03f, 0.05f, 0.10f, 1.0f},
            .pos   = {0, 0, 0}
        });

        // Far plane
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Background);
            l.rounded_rect("r", {
                .size   = {1200, 400},
                .radius = 24,
                .color  = Color{0.10f, 0.14f, 0.35f, 0.8f},
                .pos    = {0, -160, 0}
            });
        });

        // Subject plane
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Subject);
            l.rounded_rect("hero", {
                .size   = {600, 350},
                .radius = 40,
                .color  = Color{0.18f, 0.26f, 0.65f, 1.0f},
                .pos    = {0, 0, 0}
            });
            TextStyle ts{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size      = 48.0f,
                .color     = Color{1, 1, 1, 1}
            };
            l.text("label", {
                .content = "FOV MODE",
                .style   = ts,
                .pos     = {-140, -24, 0}
            });
        });

        // Near plane
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Foreground).opacity(0.55f);
            l.rounded_rect("r", {
                .size   = {320, 900},
                .radius = 0,
                .color  = Color{0.04f, 0.06f, 0.16f, 0.8f},
                .pos    = {700, 0, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("First2_5DShotFov", First2_5DShotFov)
