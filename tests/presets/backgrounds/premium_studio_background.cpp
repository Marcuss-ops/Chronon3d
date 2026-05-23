#include <tests/presets/backgrounds/premium_studio_background.hpp>
#include <chronon3d/presets/studio_presets.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace chronon3d::presets::backgrounds {

void premium_studio_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const PremiumStudioBackgroundParams& params
) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    // 1. Camera Animation
    if (params.camera_motion) {
        const float cam_x = std::sin(time * 1.2f) * 80.0f;
        const float cam_y = std::cos(time * 0.96f) * 15.0f;

        s.camera().set({
            .enabled = true,
            .position = {cam_x, cam_y, -1000.0f},
            .zoom = 1000.0f,
            .dof = {
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.014f,
                .max_blur = 12.0f
            }
        });
    }

    // 2. Select Colors Based on Mood
    Color bg_color;
    Color glow_l_color;
    Color glow_r_color;
    Color grid_color;
    Color accent_color;

    switch (params.mood) {
        case StudioMood::Cyber:
            bg_color     = Color{0.02f, 0.01f, 0.04f, 1.0f};
            glow_l_color = Color{0.0f, 0.8f, 1.0f, 0.22f};      // Neon Cyan
            glow_r_color = Color{1.0f, 0.0f, 0.8f, 0.18f};      // Neon Magenta
            grid_color   = Color{0.0f, 0.6f, 1.0f, 0.10f};
            accent_color = Color{0.0f, 0.9f, 1.0f, 0.80f};
            break;
        case StudioMood::Minimal:
            bg_color     = Color{0.06f, 0.06f, 0.08f, 1.0f};
            glow_l_color = Color{0.45f, 0.55f, 0.70f, 0.15f};    // Slate Blue
            glow_r_color = Color{0.35f, 0.40f, 0.55f, 0.12f};
            grid_color   = Color{0.50f, 0.60f, 0.75f, 0.06f};
            accent_color = Color{0.70f, 0.80f, 0.95f, 0.60f};
            break;
        case StudioMood::Luxury:
            bg_color     = Color{0.03f, 0.025f, 0.015f, 1.0f};
            glow_l_color = Color{0.85f, 0.65f, 0.25f, 0.16f};    // Warm Gold
            glow_r_color = Color{0.50f, 0.35f, 0.15f, 0.12f};
            grid_color   = Color{0.80f, 0.60f, 0.30f, 0.08f};
            accent_color = Color{0.95f, 0.75f, 0.40f, 0.75f};
            break;
    }

    // 3. Base Background (2D) — constant solid color, never changes
    s.layer("studio_premium_base", [bg_color](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg_rect", bg_color);
    });

    // 4a. Static 3D backstage: grid plane only — constant, cache forever
    s.layer("studio_premium_grid", [grid_color](LayerBuilder& l) {
        l.cache_static();
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        // Tilted Grid Floor
        l.grid_plane("floor_grid", GridPlaneParams{
            .pos = {0.0f, 260.0f, -200.0f},
            .axis = PlaneAxis::XZ,
            .extent = 4000.0f,
            .spacing = 160.0f,
            .color = grid_color,
            .fade_distance = 1800.0f,
            .fade_min_alpha = 0.0f
        });
    });

    // 4b. Animated 3D backstage: glow orbs only — position changes every frame
    s.layer("studio_premium_glows", [glow_l_color, glow_r_color, time, params](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        // Left Glow Orb
        float offset_lx = params.camera_motion ? std::sin(time * 1.0f) * 50.0f : 0.0f;
        l.circle("glow_l", CircleParams{
            .radius = 350.0f,
            .color = glow_l_color,
            .pos = {-450.0f + offset_lx, 100.0f, 0.0f}
        }).blur(72.0f);

        // Right Glow Orb
        float offset_rx = params.camera_motion ? std::cos(time * 1.2f) * 40.0f : 0.0f;
        l.circle("glow_r", CircleParams{
            .radius = 300.0f,
            .color = glow_r_color,
            .pos = {450.0f + offset_rx, -100.0f, -50.0f}
        }).blur(60.0f);
    });

    // 5. Contact Shadow for Central Glass Panel (z = 10.0f, placed behind the glass z=0)
    ContactShadowParams shadow;
    shadow.pos = {0.0f, 245.0f, 10.0f};
    shadow.size = {750.0f, 40.0f};
    shadow.blur = 24.0f;
    shadow.opacity = 0.55f;
    shadow.color = Color{0.0f, 0.0f, 0.0f, 1.0f};
    chronon3d::presets::Studio::contact_shadow(s, "studio_glass_shadow", shadow);

    // 6. Central Glass Panel (z = 0.0f, in-focus) — no animation, cache forever
    s.layer("studio_glass_panel", [accent_color](LayerBuilder& l) {
        l.cache_static();
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});
        
        // Draw the glass panel base
        l.kind(LayerKind::Glass);
        l.rounded_rect("glass_panel", RoundedRectParams{
            .size = {760.0f, 460.0f},
            .radius = 24.0f,
            .color = Color{0.05f, 0.05f, 0.05f, 0.85f} // Glass opacity and base color
        }).blur(18.0f); // Soft glass blur
    });

    // Glass panel borders & accent lines (also at z = 0.0f, overlayed on glass)
    s.layer("studio_glass_accents", [accent_color, grid_color, time, params](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, -1.0f}); // slightly in front of the glass surface

        // Outermost thin glowing border
        l.rounded_rect("glass_outline", RoundedRectParams{
            .size = {760.0f, 460.0f},
            .radius = 24.0f,
            .color = grid_color * 1.5f
        });

        // Top accent highlight
        l.rect("neon_header", RectParams{
            .size = {660.0f, 2.5f},
            .color = accent_color,
            .pos = {0.0f, -218.0f, 0.0f}
        });

        // Animated neon scanner bar sweeping across the glass panel
        if (params.camera_motion) {
            float sweep_t = std::sin(time * 1.5f);
            float sweep_x = sweep_t * 330.0f;
            l.rect("scanner_bar", RectParams{
                .size = {3.0f, 400.0f},
                .color = accent_color * 0.4f,
                .pos = {sweep_x, 0.0f, 0.0f}
            });
        }
    });

    // 7. Ambient particles (z = -80.0f, slightly out of focus)
    if (params.particles) {
        s.layer("studio_premium_particles", [accent_color, time, params](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, 0.0f, -80.0f});

            for (int i = 0; i < 12; ++i) {
                float seed_x = std::sin(static_cast<float>(i) * 44.55f) * 800.0f;
                float seed_y = std::cos(static_cast<float>(i) * 11.22f) * 450.0f;
                float seed_z = std::sin(static_cast<float>(i) * 77.66f) * 50.0f;
                float speed = 0.25f + std::abs(std::sin(static_cast<float>(i) * 5.1f)) * 0.55f;
                float size = 1.5f + std::abs(std::cos(static_cast<float>(i) * 2.7f)) * 2.0f;
                float alpha = 0.10f + std::abs(std::sin(static_cast<float>(i) * 9.8f)) * 0.35f;

                float offset_y = params.camera_motion
                    ? std::sin(time * speed * 1.35f + static_cast<float>(i) * 0.4f) * 450.0f
                    : 0.0f;
                float y_pos = seed_y + offset_y;

                l.circle("particle_" + std::to_string(i), CircleParams{
                    .radius = size,
                    .color = Color{accent_color.r, accent_color.g, accent_color.b, alpha},
                    .pos = {seed_x, y_pos, seed_z}
                });
            }
        });
    }
}

} // namespace chronon3d::presets::backgrounds
