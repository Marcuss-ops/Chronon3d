#include <tests/presets/backgrounds/studio_grid_background.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::presets::backgrounds {

void studio_grid_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const StudioGridBackgroundParams& params
) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    // 1. Camera Drift
    if (params.animated) {
        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.effective_frame()) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float drift_x = std::sin(t * 3.14159265f) * params.camera_drift;
        const float drift_y = std::cos(t * 3.14159265f) * (params.camera_drift * 0.2f);

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {drift_x, drift_y, -1000.0f};
        cam.zoom = 1000.0f;
        cam.point_of_interest = {0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);
    }

    // 2. Base Dark Background (2D Screen Space Layer) — never changes, cache forever
    s.layer("studio_grid_base", [params](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg_rect", params.background);
    });

    // 3a. Static 3D layer: grid planes only — always identical regardless of frame
    s.layer("studio_grid_static_3d", [params](LayerBuilder& l) {
        l.cache_static();
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        // 3D Grid Plane (XZ Plane, Tilted floor representation)
        l.grid_plane("grid", GridPlaneParams{
            .pos = {0.0f, 220.0f, -200.0f},
            .axis = PlaneAxis::XZ,
            .extent = 4000.0f,
            .spacing = params.spacing,
            .color = params.grid_color,
            .fade_distance = 1800.0f,
            .fade_min_alpha = 0.0f
        });

        // 3D Grid Plane Ceiling (optional secondary grid for scale)
        l.grid_plane("grid_ceiling", GridPlaneParams{
            .pos = {0.0f, -220.0f, -200.0f},
            .axis = PlaneAxis::XZ,
            .extent = 4000.0f,
            .spacing = params.spacing,
            .color = params.grid_color * 0.4f,
            .fade_distance = 1800.0f,
            .fade_min_alpha = 0.0f
        });
    });

    // 3b. Animated 3D layer: glow orbs + particles — changes every frame
    s.layer("studio_grid_anim_3d", [params, ctx, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        // Left Glow Orb
        float glow_lx = -400.0f;
        float glow_ly = 100.0f;
        if (params.animated) {
            glow_lx += std::sin(time * 0.9f) * 60.0f;
            glow_ly += std::cos(time * 0.6f) * 40.0f;
        }
        l.circle("glow_left", CircleParams{
            .radius = params.glow_radius * 2.5f,
            .color = params.glow_color,
            .pos = {glow_lx, glow_ly, 50.0f}
        }).blur(std::max(72.0f, params.glow_radius * 0.75f));

        // Right Glow Orb
        float glow_rx = 400.0f;
        float glow_ry = -100.0f;
        if (params.animated) {
            glow_rx += std::cos(time * 0.72f) * 50.0f;
            glow_ry += std::sin(time * 1.08f) * 30.0f;
        }
        l.circle("glow_right", CircleParams{
            .radius = params.glow_radius * 2.0f,
            .color = params.glow_color * 0.65f,
            .pos = {glow_rx, glow_ry, 0.0f}
        }).blur(std::max(56.0f, params.glow_radius * 0.56f));

        // Floating ambient micro-particles
        for (int i = 0; i < 20; ++i) {
            float seed_x = std::sin(static_cast<float>(i) * 12.34f) * 900.0f;
            float seed_y = std::cos(static_cast<float>(i) * 56.78f) * 500.0f;
            float seed_z = std::sin(static_cast<float>(i) * 89.12f) * 200.0f;
            float speed = 0.3f + std::abs(std::sin(static_cast<float>(i) * 4.5f)) * 0.7f;
            float size = 1.5f + std::abs(std::cos(static_cast<float>(i) * 2.3f)) * 2.5f;
            float alpha = 0.15f + std::abs(std::sin(static_cast<float>(i) * 6.7f)) * 0.45f;

            const float offset_y = params.animated
                ? std::sin(time * speed * 1.4f + static_cast<float>(i) * 0.7f) * 500.0f
                : 0.0f;

            Color part_color = params.glow_color * 1.5f;
            part_color.a = alpha;

            float y_pos = seed_y + offset_y;

            l.circle("particle_" + std::to_string(i), CircleParams{
                .radius = size,
                .color = part_color,
                .pos = {seed_x, y_pos, seed_z}
            });
        }
    });
}

} // namespace chronon3d::presets::backgrounds
