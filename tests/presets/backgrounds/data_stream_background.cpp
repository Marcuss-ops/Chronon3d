#include <tests/presets/backgrounds/data_stream_background.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <cmath>
#include <string>

namespace chronon3d::presets::backgrounds {

void data_stream_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    const DataStreamBackgroundParams& params
) {
    // 1. Slow camera drift
    if (params.animated) {
        const float time = static_cast<float>(ctx.frame) * 0.008f;
        const float cam_x = std::sin(time) * 45.0f;
        const float cam_y = std::cos(time * 0.9f) * 15.0f;
        const float cam_z = -1000.0f + std::sin(time * 0.4f) * 40.0f;

        s.camera().set({
            .enabled = true,
            .position = {cam_x, cam_y, cam_z},
            .point_of_interest = {0.0f, 0.0f, 0.0f},
            .point_of_interest_enabled = true,
            .zoom = 1000.0f
        });
    }

    // 2. Base Dark Background (2D) — constant color, cache forever
    s.layer("data_stream_base", [params](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg_rect", params.background);
    });

    // 3. Data Streams Layer (3D)
    s.layer("data_streams", [params, ctx](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 300.0f});

        for (i32 i = 0; i < params.density; ++i) {
            float x = -980.0f + (1960.0f / static_cast<float>(params.density - 1)) * static_cast<float>(i);

            // Subtle vertical timeline track
            l.line("guide_" + std::to_string(i), LineParams{
                .from = {x, -600.0f, 0.0f},
                .to = {x, 600.0f, 0.0f},
                .thickness = 0.5f,
                .color = params.stream_color * 0.08f
            });

            // Multi-speed cascade logic
            float speed_factor = 1.0f + std::abs(std::sin(static_cast<float>(i) * 2.3f)) * 1.8f;
            float vertical_offset = static_cast<float>(i) * 137.0f;
            
            float y = -600.0f;
            if (params.animated) {
                y += std::fmod(static_cast<float>(ctx.frame) * params.speed * speed_factor + vertical_offset, 1300.0f);
            } else {
                y += std::fmod(vertical_offset, 1300.0f);
            }
            if (y > 650.0f) y -= 1300.0f;

            // Stream block opacity and height random variations
            float opacity_mod = 0.3f + std::abs(std::sin(static_cast<float>(i) * 5.4f)) * 0.7f;
            float height_mod = 12.0f + std::abs(std::cos(static_cast<float>(i) * 3.7f)) * 18.0f;
            
            Color main_color = params.stream_color;
            main_color.a = opacity_mod * params.stream_color.a;

            // Cascading primary data pill
            l.rect("block_main_" + std::to_string(i), RectParams{
                .size = {2.5f, height_mod},
                .color = main_color,
                .pos = {x, y, 5.0f}
            });

            // Accent light node at the front of each stream
            Color head_color = Color{1.0f, 1.0f, 1.0f, main_color.a * 1.5f};
            l.rect("block_head_" + std::to_string(i), RectParams{
                .size = {3.5f, 3.5f},
                .color = head_color,
                .pos = {x, y + height_mod * 0.5f, 7.0f}
            });

            // Falling trailing particles
            l.rect("block_trail_" + std::to_string(i), RectParams{
                .size = {1.5f, height_mod * 0.6f},
                .color = main_color * 0.5f,
                .pos = {x, y - height_mod * 0.9f, 2.0f}
            });
        }

        // Apply neon flow glow to the entire layer
        l.glow(params.glow_radius, 0.45f, params.stream_color);
    });
}

} // namespace chronon3d::presets::backgrounds
