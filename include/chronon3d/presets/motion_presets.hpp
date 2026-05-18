#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/layer_fx.hpp>
#include <chronon3d/animation/spring.hpp>
#include <chronon3d/animation/interpolate.hpp>

namespace chronon3d::presets::motion {

/**
 * Renders a stunning spring-animated Title Card.
 */
inline void title_card(
    SceneBuilder& s,
    const std::string& title_content,
    const std::string& subtitle_content,
    Color bg_color,
    Color text_color,
    const FrameContext& ctx
) {
    // 1. Fullscreen background layer
    s.screen_layer("background", [bg_color](LayerBuilder& l) {
        l.fill(bg_color);
    });

    // 2. Spring Scale-in for Title Text
    f32 title_scale = spring(ctx.frame, ctx.frame_rate, 0.0f, 1.0f, Spring::Bouncy);
    f32 title_opacity = spring(ctx.frame, ctx.frame_rate, 0.0f, 1.0f, Spring::Gentle);

    soft_glow_text(s, "title", {
        .text = title_content,
        .font_path_main = "assets/fonts/Inter-Bold.ttf",
        .font_path_glow = "assets/fonts/Inter-Regular.ttf",
        .motion = {
            .enabled = false,
            .position = {0.0f, -40.0f, 0.0f},
            .scale = {title_scale, title_scale, 1.0f},
        },
        .font_size = 80.0f,
        .outer_size_boost = 10.0f,
        .inner_size_boost = 4.0f,
        .outer_blur = 24.0f,
        .inner_blur = 12.0f,
        .outer_opacity = 0.16f * title_opacity,
        .inner_opacity = 0.30f * title_opacity,
        .tracking = 0.5f,
        .align = TextAlign::Center,
        .main_color = text_color.with_alpha(title_opacity),
        .glow_color = text_color.with_alpha(1.0f),
    });

    // 3. Staggered Spring-in for Subtitle
    // Subtitle animations start at frame 10 (staggered delay)
    Frame sub_frame = ctx.frame > 10 ? Frame{ctx.frame - 10} : Frame{0};
    f32 sub_scale = spring(sub_frame, ctx.frame_rate, 0.0f, 1.0f, Spring::Snappy);
    f32 sub_opacity = spring(sub_frame, ctx.frame_rate, 0.0f, 1.0f, Spring::Gentle);

    s.layer("subtitle", [subtitle_content, text_color, sub_scale, sub_opacity](LayerBuilder& l) {
        l.position({0.0f, 60.0f, 0.0f})
         .scale({sub_scale, sub_scale, 1.0f})
         .opacity(sub_opacity)
         .text("subtitle_text", {
             .content = subtitle_content,
             .style = {
                 .font_family = "Inter",
                 .font_weight = 400,
                 .size = 36.0f,
                 .color = text_color.with_alpha(0.7f),
                 .align = TextAlign::Center
             }
         });
    });
}

/**
 * Renders a gorgeous lower-third overlay at the bottom left of the screen.
 */
inline void lower_third(
    SceneBuilder& s,
    const std::string& line1,
    const std::string& line2,
    const FrameContext& ctx
) {
    // Slip in from the left: 0 to 30 frames
    f32 slide_x = spring(ctx.frame, ctx.frame_rate, -800.0f, 0.0f, Spring::Snappy);
    f32 opacity = spring(ctx.frame, ctx.frame_rate, 0.0f, 1.0f, Spring::Gentle);

    // Render local sequence bounds
    s.screen_layer("lower_third_container", [line1, line2, slide_x, opacity, &ctx](LayerBuilder& l) {
        // Move container to bottom left corner
        l.position({ - (ctx.width * 0.5f) + 100.0f + slide_x, (ctx.height * 0.5f) - 200.0f, 0.0f })
         .opacity(opacity);

        // Semi-transparent background glass plate
        l.rect("plate", {
            .size = { 600.0f, 120.0f },
            .color = Color{ 0.07f, 0.07f, 0.1f, 0.8f },
            .pos = { 300.0f, 60.0f, 0.0f } // Centered within 600x120 container
        }).blur(10.0f);

        // Accent strip on the left side
        l.rect("accent", {
            .size = { 6.0f, 120.0f },
            .color = Color{ 0.3f, 0.6f, 1.0f, 1.0f },
            .pos = { 3.0f, 60.0f, 0.0f }
        });

        // Line 1 Text
        l.text("line1", {
            .content = line1,
            .style = {
                .font_family = "Inter",
                .font_weight = 700,
                .size = 28.0f,
                .color = Color::white(),
                .align = TextAlign::Left
            },
            .box = { .size = { 560.0f, 40.0f }, .enabled = true }
        }).at({ 30.0f, 45.0f, 0.0f });

        // Line 2 Text
        l.text("line2", {
            .content = line2,
            .style = {
                .font_family = "Inter",
                .font_weight = 400,
                .size = 20.0f,
                .color = Color{ 0.7f, 0.7f, 0.8f, 1.0f },
                .align = TextAlign::Left
            },
            .box = { .size = { 560.0f, 30.0f }, .enabled = true }
        }).at({ 30.0f, 85.0f, 0.0f });
    });
}

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
