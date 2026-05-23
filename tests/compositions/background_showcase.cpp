#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/core/composition_registration.hpp>

#include <tests/presets/backgrounds/studio_grid_background.hpp>
#include <tests/presets/backgrounds/gradient_orbs_background.hpp>
#include <tests/presets/backgrounds/parallax_space_background.hpp>
#include <tests/presets/backgrounds/data_stream_background.hpp>
#include <tests/presets/backgrounds/premium_studio_background.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::presets::backgrounds;

namespace {

Composition background_showcase() {
    return composition({
        .name = "BackgroundShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 180
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Partition 180 frames into 5 segments (36 frames each)
        const int segment_len = 36;
        const int segment = std::min(static_cast<int>(ctx.frame) / segment_len, 4);
        const int local_frame = static_cast<int>(ctx.frame) % segment_len;

        FrameContext segment_ctx = ctx;
        segment_ctx.frame = local_frame;
        segment_ctx.duration = segment_len;

        std::string subtitle_text = "";

        // Render selected background preset
        switch (segment) {
            case 0: {
                StudioGridBackgroundParams params;
                params.spacing = 110.0f;
                params.glow_radius = 130.0f;
                params.animated = true;
                studio_grid_background(s, segment_ctx, params);
                subtitle_text = "1. CINEMATIC GRID (MOTION GRAPHICS)";
                break;
            }
            case 1: {
                GradientOrbsBackgroundParams params;
                params.base_radius = 340.0f;
                params.animated = true;
                gradient_orbs_background(s, segment_ctx, params);
                subtitle_text = "2. ABSTRACT GRADIENT ORBS (COMPOSITING)";
                break;
            }
            case 2: {
                ParallaxSpaceBackgroundParams params;
                params.animated = true;
                params.dof_enabled = true;
                params.focus_z = 0.0f;
                params.max_blur = 16.0f;
                parallax_space_background(s, segment_ctx, params);
                subtitle_text = "3. PARALLAX CARD SPACE (2.5D DEPTH)";
                break;
            }
            case 3: {
                DataStreamBackgroundParams params;
                params.density = 70;
                params.speed = 3.0f;
                params.animated = true;
                data_stream_background(s, segment_ctx, params);
                subtitle_text = "4. DATA STREAM (PROCEDURAL FLOW)";
                break;
            }
            case 4:
            default: {
                PremiumStudioBackgroundParams params;
                // Alternate mood based on frame range in the final segment
                params.mood = (local_frame < 18) ? StudioMood::Cyber : StudioMood::Luxury;
                params.particles = true;
                params.camera_motion = true;
                premium_studio_background(s, segment_ctx, params);
                subtitle_text = (local_frame < 18) ? "5. PREMIUM STUDIO (CYBER GLOW)" : "5. PREMIUM STUDIO (LUXURY GOLD)";
                break;
            }
        }

        // Draw overlay title and subtitle dynamically in 3D space
        // Let's place it at z = -40.0f (in front of the glass/grids) to stand out
        s.layer("showcase_titles", [subtitle_text](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, -80.0f, -40.0f});

            // Showcase Header
            l.text("main_title", TextParams{
                .text = "CHRONON3D ENGINE PRESETS",
                .size = {1400.0f, 150.0f},
                .pos = {0.0f, -40.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 72.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 0.95f},
                .align = TextAlign::Center
            });

            // Glowing subtitle representing active background preset
            l.text("subtitle", TextParams{
                .text = subtitle_text,
                .size = {1400.0f, 100.0f},
                .pos = {0.0f, 50.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 32.0f,
                .color = Color{0.45f, 0.75f, 1.0f, 1.0f},
                .align = TextAlign::Center
            });

            // Add dynamic neon glow to title layer
            l.glow(18.0f, 0.65f, Color{0.2f, 0.5f, 1.0f, 1.0f});
        });

        // Corner info badge (2D Screen Space Overlay)
        s.layer("showcase_info_badge", [](LayerBuilder& l) {
            l.rounded_rect("badge_bg", RoundedRectParams{
                .size = {220.0f, 50.0f},
                .radius = 8.0f,
                .color = Color{0.05f, 0.05f, 0.08f, 0.85f},
                .pos = {800.0f, -480.0f, 0.0f}
            });

            l.text("badge_text", TextParams{
                .text = "1080P 30FPS",
                .size = {220.0f, 50.0f},
                .pos = {800.0f, -462.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 18.0f,
                .color = Color{0.9f, 0.9f, 0.95f, 0.9f},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("BackgroundShowcase", background_showcase)

namespace {

Composition studio_grid_showcase() {
    return composition({
        .name = "StudioGridShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        StudioGridBackgroundParams params;
        params.spacing = 110.0f;
        params.glow_radius = 130.0f;
        params.animated = true;
        studio_grid_background(s, ctx, params);
        return s.build();
    });
}

Composition gradient_orbs_showcase() {
    return composition({
        .name = "GradientOrbsShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        GradientOrbsBackgroundParams params;
        params.base_radius = 340.0f;
        params.animated = true;
        gradient_orbs_background(s, ctx, params);
        return s.build();
    });
}

Composition parallax_space_showcase() {
    return composition({
        .name = "ParallaxSpaceShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        ParallaxSpaceBackgroundParams params;
        params.animated = true;
        params.dof_enabled = true;
        params.focus_z = 0.0f;
        params.max_blur = 16.0f;
        parallax_space_background(s, ctx, params);
        return s.build();
    });
}

Composition data_stream_showcase() {
    return composition({
        .name = "DataStreamShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        DataStreamBackgroundParams params;
        params.density = 70;
        params.speed = 3.0f;
        params.animated = true;
        data_stream_background(s, ctx, params);
        return s.build();
    });
}

Composition premium_studio_showcase() {
    return composition({
        .name = "PremiumStudioShowcase",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        PremiumStudioBackgroundParams params;
        params.mood = StudioMood::Cyber;
        params.particles = true;
        params.camera_motion = true;
        premium_studio_background(s, ctx, params);
        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("StudioGridShowcase", studio_grid_showcase)
CHRONON_REGISTER_COMPOSITION("GradientOrbsShowcase", gradient_orbs_showcase)
CHRONON_REGISTER_COMPOSITION("ParallaxSpaceShowcase", parallax_space_showcase)
CHRONON_REGISTER_COMPOSITION("DataStreamShowcase", data_stream_showcase)
CHRONON_REGISTER_COMPOSITION("PremiumStudioShowcase", premium_studio_showcase)

