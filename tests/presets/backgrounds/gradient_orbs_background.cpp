#include <tests/presets/backgrounds/gradient_orbs_background.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <cmath>

namespace chronon3d::presets::backgrounds {

void gradient_orbs_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const GradientOrbsBackgroundParams& params
) {
    // 1. Camera animation (very slow drift)
    if (params.animated) {
        const float time = static_cast<float>(ctx.frame) * 0.005f;
        const float cam_x = std::sin(time) * 30.0f;
        const float cam_y = std::cos(time * 0.7f) * 15.0f;

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {cam_x, cam_y, -1000.0f};
        cam.zoom = 1000.0f;
        cam.point_of_interest = {0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);
    }

    // 2. Base dark blue-ish background — constant color, cache forever
    s.layer("gradient_orbs_base", [params](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg_rect", params.background);
    });

    // 3. Orbs layer (blurred together for blending)
    s.layer("gradient_orbs_blurred", [params, ctx](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 400.0f});

        for (size_t i = 0; i < params.orb_colors.size(); ++i) {
            float speed_factor = params.speed_multiplier * (1.0f + 0.15f * static_cast<float>(i));
            float phase_offset = static_cast<float>(i) * 2.094f; // ~120 degrees offset
            float time = params.animated ? (static_cast<float>(ctx.frame) * 0.012f * speed_factor) : 0.0f;

            float x = std::sin(time + phase_offset) * 450.0f;
            float y = std::cos(time * 0.8f + phase_offset) * 250.0f;
            float z = std::sin(time * 0.5f + phase_offset) * 150.0f;

            float radius = params.base_radius * (0.9f + 0.3f * std::abs(std::sin(time * 0.4f)));

            l.circle("orb_" + std::to_string(i), CircleParams{
                .radius = radius,
                .color = params.orb_colors[i],
                .pos = {x, y, z}
            });
        }

        // Substantial blur to blend orbs into gradient shapes
        l.blur(110.0f);
    });

    // 4. Fine grain particles (sharp, on top of the blur)
    if (params.particles) {
        s.layer("gradient_orbs_grain", [ctx](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, 0.0f, 100.0f}); // Forward layer to stay sharp and in front

            for (int i = 0; i < 50; ++i) {
                float seed_x = std::sin(static_cast<float>(i) * 34.56f) * 960.0f;
                float seed_y = std::cos(static_cast<float>(i) * 78.90f) * 540.0f;
                float seed_z = std::sin(static_cast<float>(i) * 12.34f) * 150.0f;
                float speed = 0.15f + std::abs(std::sin(static_cast<float>(i) * 5.5f)) * 0.35f;
                float size = 1.0f + std::abs(std::cos(static_cast<float>(i) * 1.7f)) * 1.5f;
                float alpha = 0.05f + std::abs(std::sin(static_cast<float>(i) * 9.1f)) * 0.15f;

                // Slow upward scroll
                float offset_y = -std::fmod(static_cast<float>(ctx.frame) * speed, 1100.0f);
                float y_pos = seed_y + offset_y;
                if (y_pos < -550.0f) y_pos += 1100.0f;

                l.circle("grain_" + std::to_string(i), CircleParams{
                    .radius = size,
                    .color = Color{1.0f, 1.0f, 1.0f, alpha},
                    .pos = {seed_x, y_pos, seed_z}
                });
            }
        });
    }
}

} // namespace chronon3d::presets::backgrounds
