#include <tests/presets/backgrounds/parallax_space_background.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace chronon3d::presets::backgrounds {

void parallax_space_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const ParallaxSpaceBackgroundParams& params
) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    // 1. Setup camera and DOF
    if (params.animated) {
        const float cam_x = std::sin(time * 1.44f) * 120.0f;
        const float cam_y = std::cos(time * 1.08f) * 25.0f;

        s.camera().set({
            .enabled = true,
            .position = {cam_x, cam_y, -1000.0f},
            .zoom = 1000.0f,
            .dof = {
                .enabled = params.dof_enabled,
                .focus_z = params.focus_z,
                .aperture = 0.016f,
                .max_blur = params.max_blur
            }
        });
    } else {
        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, -1000.0f},
            .zoom = 1000.0f,
            .dof = {
                .enabled = params.dof_enabled,
                .focus_z = params.focus_z,
                .aperture = 0.016f,
                .max_blur = params.max_blur
            }
        });
    }

    // 2. Base Background Layer (2D) — constant color, cache forever
    s.layer("parallax_base", [params](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg_rect", params.background);
    });

    // 3. Background Layer (z = 600.0f)
    s.layer("parallax_z_far_bg", [params](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 600.0f});

        l.circle("bg_glow", CircleParams{
            .radius = 450.0f,
            .color = Color{0.08f, 0.15f, 0.35f, 0.40f},
            .pos = {0.0f, 0.0f, 0.0f}
        }).blur(100.0f);

        // Distant space grids
        l.grid_plane("far_grid", GridPlaneParams{
            .pos = {0.0f, 180.0f, 0.0f},
            .axis = PlaneAxis::XZ,
            .extent = 3000.0f,
            .spacing = 150.0f,
            .color = Color{0.2f, 0.4f, 0.8f, 0.08f},
            .fade_distance = 1500.0f,
            .fade_min_alpha = 0.0f
        });
    });

    // 4. Far Cards Layer (z = 400.0f) — no animated coordinates, cache forever
    s.layer("parallax_z_far_cards", [params](LayerBuilder& l) {
        l.cache_static();
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        // 3 distant cards
        for (int i = 0; i < 3; ++i) {
            float x = -400.0f + static_cast<float>(i) * 400.0f;
            float y = std::sin(static_cast<float>(i) * 1.5f) * 100.0f;
            
            l.rounded_rect("far_card_" + std::to_string(i), RoundedRectParams{
                .size = {280.0f, 180.0f},
                .radius = 12.0f,
                .color = Color{0.15f, 0.2f, 0.4f, 0.18f},
                .pos = {x, y, 0.0f}
            });

            // Accent outline/line inside card
            l.rect("far_card_accent_" + std::to_string(i), RectParams{
                .size = {250.0f, 2.0f},
                .color = Color{0.3f, 0.5f, 1.0f, 0.35f},
                .pos = {x, y - 80.0f, 1.0f}
            });
        }
    });

    // 5. Mid Cards Layer (z = 200.0f)
    s.layer("parallax_z_mid_cards", [params, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 200.0f});

        // 3 mid cards with slight animation
        for (int i = 0; i < 3; ++i) {
            float offset_y = params.animated ? std::sin(time * 1.2f + static_cast<float>(i)) * 25.0f : 0.0f;
            float x = -500.0f + static_cast<float>(i) * 500.0f;
            float y = -120.0f + static_cast<float>(i % 2) * 200.0f + offset_y;

            l.rounded_rect("mid_card_" + std::to_string(i), RoundedRectParams{
                .size = {350.0f, 220.0f},
                .radius = 16.0f,
                .color = Color{0.2f, 0.25f, 0.5f, 0.25f},
                .pos = {x, y, 0.0f}
            });

            // Card highlight line
            l.rect("mid_card_line_" + std::to_string(i), RectParams{
                .size = {310.0f, 1.5f},
                .color = Color{0.4f, 0.6f, 1.0f, 0.55f},
                .pos = {x, y + 95.0f, 1.0f}
            });
        }
    });

    // 6. Near Cards (Hero level, z = 0.0f) - Completely sharp — static elements, cache forever
    s.layer("parallax_z_hero", [params, ctx](LayerBuilder& l) {
        l.cache_static();
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});

        // Hero central glass panels (empty frames for showcase text)
        l.rounded_rect("hero_panel_left", RoundedRectParams{
            .size = {420.0f, 280.0f},
            .radius = 20.0f,
            .color = Color{0.05f, 0.08f, 0.18f, 0.75f},
            .pos = {-350.0f, 0.0f, 0.0f}
        });

        l.rounded_rect("hero_panel_right", RoundedRectParams{
            .size = {420.0f, 280.0f},
            .radius = 20.0f,
            .color = Color{0.05f, 0.08f, 0.18f, 0.75f},
            .pos = {350.0f, 0.0f, 0.0f}
        });

        // Top neon borders for the hero panels
        l.rect("hero_panel_l_neon", RectParams{
            .size = {420.0f, 3.0f},
            .color = Color{0.3f, 0.7f, 1.0f, 1.0f},
            .pos = {-350.0f, -138.5f, 1.0f}
        });

        l.rect("hero_panel_r_neon", RectParams{
            .size = {420.0f, 3.0f},
            .color = Color{0.8f, 0.3f, 1.0f, 1.0f},
            .pos = {350.0f, -138.5f, 1.0f}
        });
    });

    // 7. Foreground particles close to the camera (z = -150.0f) - Deeply blurred
    s.layer("parallax_z_foreground", [params, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, -150.0f});

        for (int i = 0; i < 15; ++i) {
            float seed_x = std::sin(static_cast<float>(i) * 55.44f) * 650.0f;
            float seed_y = std::cos(static_cast<float>(i) * 22.33f) * 350.0f;
            float seed_z = std::sin(static_cast<float>(i) * 99.88f) * 80.0f;
            float speed = 0.5f + std::abs(std::sin(static_cast<float>(i) * 3.3f)) * 0.8f;
            float size = 4.0f + std::abs(std::cos(static_cast<float>(i) * 1.5f)) * 6.0f;
                float alpha = 0.15f + std::abs(std::sin(static_cast<float>(i) * 8.7f)) * 0.35f;

                float offset_y = params.animated
                    ? std::sin(time * speed * 1.7f + static_cast<float>(i) * 0.5f) * 400.0f
                    : 0.0f;
                float y_pos = seed_y + offset_y;

            l.circle("foreground_particle_" + std::to_string(i), CircleParams{
                .radius = size,
                .color = Color{0.45f, 0.75f, 1.0f, alpha},
                .pos = {seed_x, y_pos, seed_z}
            });
        }
    });
}

} // namespace chronon3d::presets::backgrounds
