#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/spring.hpp>
#include <chronon3d/animation/interpolate.hpp>

namespace chronon3d::presets::motion {

/**
 * Renders a clean premium video Progress Bar at the bottom edge.
 */
inline void progress_bar(
    SceneBuilder& s,
    f32 progress,
    Color bar_color,
    const FrameContext& ctx
) {
    f32 bar_height = 8.0f;
    f32 full_width = static_cast<f32>(ctx.width);

    s.screen_layer("progress_bar", [progress, bar_color, bar_height, full_width](LayerBuilder& l) {
        l.position({0.0f, 0.0f, 0.0f});

        // Background track (dark transparent)
        l.rect("track", {
            .size = { full_width, bar_height },
            .color = Color{ 0.1f, 0.1f, 0.15f, 0.5f },
            .pos = { 0.0f, static_cast<f32>(1080.0f * 0.5f - bar_height * 0.5f), 0.0f }
        });

        // Filled progress bar (left-aligned)
        f32 filled_width = full_width * std::clamp(progress, 0.0f, 1.0f);
        l.rect("fill", {
            .size = { filled_width, bar_height },
            .color = bar_color,
            .pos = { - (full_width * 0.5f) + (filled_width * 0.5f), static_cast<f32>(1080.0f * 0.5f - bar_height * 0.5f), 0.0f }
        });
    });
}

} // namespace chronon3d::presets::motion
