#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <string>
#include <cmath>

#include "text_helpers.hpp"
#include <chronon3d/animation/interpolate.hpp>

namespace chronon3d::content::text {

Composition text_title_big() {
    return composition({
        .name = "TextTitleBig",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        Color white{1, 1, 1, 1};
        Color gray{0.8f, 0.8f, 0.85f, 1};
        Color accent{0.25f, 0.52f, 1.0f, 1};

        f32 progress = ctx.progress();
        f32 title_opacity = std::min(1.0f, progress * 4.0f);
        f32 sub_opacity = std::min(1.0f, std::max(0.0f, (progress - 0.15f) * 4.0f));

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.010f, 0.022f, 1.0f});
        });

        s.layer("title", [&](LayerBuilder& l) {
            l.opacity(title_opacity).pin_to(Anchor::Center);
            apply_text(l, "title", {
                .text = "CHRONON 3D",
                .size = 120.0f,
                .color = white,
                .align = TextAlign::Center,
                .tracking = 12.0f,
            }, {W * 0.85f, 160.0f}, {0, -60, 0});
        });

        s.layer("subtitle", [&](LayerBuilder& l) {
            l.opacity(sub_opacity).pin_to(Anchor::Center);
            apply_text(l, "sub", {
                .text = "Content Generation Pipeline",
                .size = 40.0f,
                .color = gray,
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {W * 0.7f, 80.0f}, {0, 80, 0});
        });

        s.layer("accent_line", [&](LayerBuilder& l) {
            f32 line_scale = std::min(1.0f, (progress - 0.05f) * 5.0f);
            l.opacity(title_opacity).position({0, 0, 0}).scale({line_scale, 1, 1});
            l.rect("line", {
                .size = {200.0f, 3.0f},
                .color = accent,
                .pos = {0, 120, 0},
            });
        });

        return s.build();
    });
}

Composition text_subtitle() {
    return composition({
        .name = "TextSubtitle",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 progress = ctx.progress();
        f32 opacity = std::min(1.0f, progress * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.02f, 0.02f, 0.03f, 1.0f});
        });

        s.layer("subtitle_bg", [&](LayerBuilder& l) {
            l.opacity(opacity * 0.85f)
             .pin_to(Anchor::BottomCenter, 80.0f)
             .rounded_rect("bg", {
                .size = {W * 0.6f, 90.0f},
                .radius = 12.0f,
                .color = Color{0, 0, 0, 0.65f},
            });
        });

        s.layer("subtitle_text", [&](LayerBuilder& l) {
            l.opacity(opacity).pin_to(Anchor::BottomCenter, 85.0f);
            apply_text(l, "text", {
                .text = "Exploring the frontiers of real-time rendering",
                .size = 36.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 1.5f,
            }, {W * 0.55f, 60.0f});
        });

        return s.build();
    });
}

Composition text_lower_third() {
    return composition({
        .name = "TextLowerThird",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 bar_w = std::min(1.0f, p * 5.0f) * 720.0f;
        f32 text_op = std::min(1.0f, std::max(0.0f, (p - 0.08f) * 6.0f));

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.012f, 0.025f, 1.0f});
        });

        s.layer("bar", [&](LayerBuilder& l) {
            l.pin_to(Anchor::BottomLeft, 120.0f);
            l.rect("bar_shape", {
                .size = {bar_w, 8.0f},
                .color = {0.25f, 0.52f, 1.0f, 1},
            });
        });

        s.layer("name_bg", [&](LayerBuilder& l) {
            l.pin_to(Anchor::BottomLeft, 80.0f);
            l.rect("name_bg_shape", {
                .size = {std::min(bar_w, 480.0f), 48.0f},
                .color = {0.25f, 0.52f, 1.0f, 0.18f},
            });
        });

        s.layer("name", [&](LayerBuilder& l) {
            l.opacity(text_op).pin_to(Anchor::BottomLeft, 88.0f);
            apply_text(l, "name_text", {
                .text = "PIERONE",
                .size = 30.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Left,
                .tracking = 6.0f,
            }, {W * 0.3f, 40.0f}, {40, 0, 0});
        });

        s.layer("title_l3", [&](LayerBuilder& l) {
            l.opacity(text_op).pin_to(Anchor::BottomLeft, 130.0f);
            apply_text(l, "title_text", {
                .text = "Content Generation System Demo",
                .size = 42.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .tracking = 1.0f,
            }, {W * 0.5f, 50.0f}, {40, 0, 0});
        });

        return s.build();
    });
}

Composition text_cinematic_intro() {
    return composition({
        .name = "TextCinematicIntro",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        Color white{1, 1, 1, 1};
        Color accent{0.25f, 0.52f, 1.0f, 1};
        Color neon_glow{0.0f, 0.8f, 1.0f, 1};

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.003f, 0.005f, 0.012f, 1.0f});
        });

        s.layer("grid", [p](LayerBuilder& l) {
            l.opacity(0.12f);
            l.grid_background("bg_grid", {
                .size = {W * 1.5f, H * 1.5f},
                .offset = {0.0f, p * 40.0f},
                .bg_color = {0, 0, 0, 0},
                .grid_color = {0.25f, 0.52f, 1.0f, 0.5f},
                .spacing = 100.0f,
                .major_every = 4,
            });
        });

        s.camera().set(chronon3d::camera_motion::dramatic_push(p, 800.0f));

        s.layer("bg_title", [p](LayerBuilder& l) {
            f32 op = interpolate(p, 0.0f, 0.5f, 0.0f, 0.15f, Easing::Linear);
            l.enable_3d().position({0, 0, 400}).opacity(op);
            apply_text(l, "bg_txt", {
                .text = "CHRONON",
                .size = 280.0f,
                .color = {0.25f, 0.52f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 40.0f,
            }, {W * 1.5f, 350.0f});
        });

        s.layer("main_title", [p, white, neon_glow](LayerBuilder& l) {
            f32 op = interpolate(p, 0.1f, 0.4f, 0.0f, 1.0f, Easing::OutCubic);
            f32 z_pos = interpolate(p, 0.1f, 0.6f, -300.0f, 0.0f, Easing::OutBack);
            l.enable_3d().position({0, -50.0f, z_pos}).opacity(op);
            l.glow(25.0f * (1.0f - p), 0.5f, neon_glow);
            
            apply_text(l, "title_txt", {
                .text = "CHRONON 3D",
                .size = 130.0f,
                .color = white,
                .align = TextAlign::Center,
                .tracking = 16.0f,
            }, {W * 0.9f, 180.0f});
        });

        s.layer("accent_line", [p, accent](LayerBuilder& l) {
            f32 w_scale = interpolate(p, 0.3f, 0.7f, 0.0f, 1.0f, Easing::OutCubic);
            f32 op = interpolate(p, 0.3f, 0.5f, 0.0f, 0.8f);
            l.enable_3d().position({0, 60.0f, 0.0f}).scale({w_scale, 1.0f, 1.0f}).opacity(op);
            l.rect("line", {
                .size = {350.0f, 4.0f},
                .color = accent,
            });
        });

        s.layer("subtitle", [p](LayerBuilder& l) {
            f32 op = interpolate(p, 0.45f, 0.85f, 0.0f, 0.85f, Easing::OutCubic);
            f32 y_offset = interpolate(p, 0.45f, 0.85f, 140.0f, 110.0f, Easing::OutCubic);
            l.enable_3d().position({0, y_offset, 0.0f}).opacity(op);
            apply_text(l, "sub_txt", {
                .text = "Headless C++ Motion Graphics Engine",
                .size = 34.0f,
                .color = {0.8f, 0.85f, 0.95f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {W * 0.8f, 60.0f});
        });

        return s.build();
    });
}

Composition text_perspective_sweep_demo() {
    return composition({
        .name = "TextPerspectiveSweepDemo",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
        });

        auto title = chronon3d::presets::motion::MotionObject::text(
            "demo_title",
            "SWEEP"
        )
            .at({0.0f, 0.0f, 2.0f})
            .size({W * 0.52f, 140.0f})
            .font_path("assets/fonts/Inter-Bold.ttf")
            .font_family("Inter")
            .font_weight(800)
            .font_size(90.0f)
            .tracking(8.0f)
            .align(chronon3d::presets::motion::TextAlign::Center)
            .vertical_align(VerticalAlign::Middle)
            .color({1.0f, 1.0f, 1.0f, 1.0f})
            .opacity(1.0f)
            .glow(true)
            .time(0, 120)
            .enable_3d()
            .parallax(2.0f)
            .preset(chronon3d::presets::motion::MotionPreset::ParallaxDrift);

        chronon3d::presets::motion::draw_motion_object(s, ctx, title);

        return s.build();
    });
}

Composition text_glow_transition() {
    return composition({
        .name = "TextGlowTransition",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.005f, 0.015f, 1.0f});
        });

        s.layer("grid", [p](LayerBuilder& l) {
            l.opacity(0.08f);
            l.grid_background("grid", {
                .size = {W, H},
                .bg_color = {0, 0, 0, 0},
                .grid_color = {0.2f, 0.5f, 1.0f, 1.0f},
                .spacing = 80.0f,
            });
        });

        s.layer("glow_layer", [p](LayerBuilder& l) {
            f32 op = interpolate(p, 0.0f, 0.4f, 0.0f, 1.0f, Easing::OutCubic);
            f32 glow_intensity = interpolate(p, 0.2f, 0.5f, 0.0f, 40.0f, Easing::OutQuad);
            if (p > 0.5f) {
                glow_intensity = interpolate(p, 0.5f, 1.0f, 40.0f, 10.0f, Easing::OutCubic);
            }
            l.opacity(op).pin_to(Anchor::Center);
            l.glow(glow_intensity, 0.8f, Color{0.0f, 0.6f, 1.0f, 1.0f});
            
            apply_text(l, "txt", {
                .text = "CREATIVE ENGINE",
                .size = 110.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 12.0f,
            }, {W * 0.8f, 160.0f}, {0, -40.0f, 0});
        });

        s.layer("sub_layer", [p](LayerBuilder& l) {
            f32 op = interpolate(p, 0.5f, 0.8f, 0.0f, 0.8f, Easing::OutCubic);
            f32 y_pos = interpolate(p, 0.5f, 0.8f, 100.0f, 70.0f, Easing::OutCubic);
            l.opacity(op).pin_to(Anchor::Center);
            apply_text(l, "sub", {
                .text = "EXPERIENCE THE NEXT LEVEL OF MOTION GRAPHICS",
                .size = 26.0f,
                .color = {0.7f, 0.7f, 0.8f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 6.0f,
            }, {W * 0.8f, 50.0f}, {0, y_pos, 0});
        });

        return s.build();
    });
}

Composition text_spring_reveal() {
    return composition({
        .name = "TextSpringReveal",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.012f, 0.018f, 1.0f});
        });

        s.layer("w1", [p](LayerBuilder& l) {
            f32 scale = interpolate(p, 0.1f, 0.5f, 0.0f, 1.0f, Easing::OutBack);
            f32 op = interpolate(p, 0.1f, 0.3f, 0.0f, 1.0f);
            l.opacity(op).pin_to(Anchor::Center).scale({scale, scale, 1.0f});
            apply_text(l, "build_txt", {
                .text = "BUILD",
                .size = 140.0f,
                .color = {0.25f, 0.52f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {400.0f, 180.0f}, {-450.0f, 0.0f, 0.0f});
        });

        s.layer("w2", [p](LayerBuilder& l) {
            f32 scale = interpolate(p, 0.3f, 0.7f, 0.0f, 1.0f, Easing::OutBack);
            f32 op = interpolate(p, 0.3f, 0.5f, 0.0f, 1.0f);
            l.opacity(op).pin_to(Anchor::Center).scale({scale, scale, 1.0f});
            apply_text(l, "render_txt", {
                .text = "RENDER",
                .size = 140.0f,
                .color = {0.0f, 0.8f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {500.0f, 180.0f}, {0.0f, 0.0f, 0.0f});
        });

        s.layer("w3", [p](LayerBuilder& l) {
            f32 scale = interpolate(p, 0.5f, 0.9f, 0.0f, 1.0f, Easing::OutBack);
            f32 op = interpolate(p, 0.5f, 0.7f, 0.0f, 1.0f);
            l.opacity(op).pin_to(Anchor::Center).scale({scale, scale, 1.0f});
            apply_text(l, "shine_txt", {
                .text = "SHINE",
                .size = 140.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {400.0f, 180.0f}, {450.0f, 0.0f, 0.0f});
        });

        return s.build();
    });
}

Composition text_cyberpunk_glitch() {
    return composition({
        .name = "TextCyberpunkGlitch",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 frame = static_cast<f32>(ctx.frame);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.002f, 0.01f, 1.0f});
        });

        s.layer("grid", [p](LayerBuilder& l) {
            l.opacity(0.15f);
            l.grid_background("grid", {
                .size = {W, H},
                .bg_color = {0, 0, 0, 0},
                .grid_color = {1.0f, 0.0f, 0.5f, 1.0f},
                .spacing = 60.0f,
            });
        });

        f32 glitch_x = 0.0f;
        f32 glitch_y = 0.0f;
        if (std::sin(frame * 0.77f) > 0.85f) {
            glitch_x = std::sin(frame * 12.3f) * 15.0f;
            glitch_y = std::cos(frame * 18.7f) * 5.0f;
        }

        s.layer("cyan_shadow", [p, glitch_x, glitch_y](LayerBuilder& l) {
            f32 op = interpolate(p, 0.0f, 0.3f, 0.0f, 0.7f);
            l.opacity(op).pin_to(Anchor::Center);
            apply_text(l, "cyan_txt", {
                .text = "CYBERPUNK",
                .size = 130.0f,
                .color = {0.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 16.0f,
            }, {W * 0.8f, 160.0f}, {-6.0f + glitch_x, glitch_y, 0.0f});
        });

        s.layer("magenta_shadow", [p, glitch_x, glitch_y](LayerBuilder& l) {
            f32 op = interpolate(p, 0.0f, 0.3f, 0.0f, 0.7f);
            l.opacity(op).pin_to(Anchor::Center);
            apply_text(l, "magenta_txt", {
                .text = "CYBERPUNK",
                .size = 130.0f,
                .color = {1.0f, 0.0f, 0.5f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 16.0f,
            }, {W * 0.8f, 160.0f}, {6.0f + glitch_x, -glitch_y, 0.0f});
        });

        s.layer("main_text", [p, glitch_x, glitch_y](LayerBuilder& l) {
            f32 op = interpolate(p, 0.0f, 0.2f, 0.0f, 1.0f);
            l.opacity(op).pin_to(Anchor::Center);
            apply_text(l, "white_txt", {
                .text = "CYBERPUNK",
                .size = 130.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .tracking = 16.0f,
            }, {W * 0.8f, 160.0f}, {glitch_x, glitch_y, 0.0f});
        });

        s.layer("scanline", [p](LayerBuilder& l) {
            f32 y_pos = -H * 0.5f + p * H;
            l.opacity(0.25f).position({0.0f, y_pos, 0.0f});
            l.rect("line", {
                .size = {W, 6.0f},
                .color = {0.0f, 1.0f, 1.0f, 1.0f},
            });
        });

        return s.build();
    });
}

Composition text_credits_3d() {
    return composition({
        .name = "TextCredits3D",
        .width = 1920, .height = 1080,
        .duration = 240,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.002f, 0.002f, 0.006f, 1.0f});
        });

        for (int i = 0; i < 15; ++i) {
            f32 x = std::sin(static_cast<f32>(i) * 45.3f) * W * 0.4f;
            f32 y = std::cos(static_cast<f32>(i) * 23.7f) * H * 0.4f;
            s.layer("star_" + std::to_string(i), [x, y](LayerBuilder& l) {
                l.enable_3d().position({x, y, 600.0f}).opacity(0.4f);
                l.circle("dot", {
                    .radius = 2.0f,
                    .color = {1, 1, 1, 1},
                });
            });
        }

        s.camera().set(chronon3d::camera_motion::dolly(p, {
            .from_z = -400.0f,
            .to_z = -220.0f,
            .position_xy = {0.0f, 350.0f, 0.0f},
            .target = {0.0f, -100.0f, 300.0f},
            .zoom = 700.0f,
        }));

        f32 scroll_z = interpolate(p, 0.0f, 1.0f, 1200.0f, -600.0f, Easing::Linear);

        struct Credit { const char* role; const char* name; };
        const Credit credits[] = {
            {"PRODUCED BY", "PIERONE"},
            {"DEVELOPED BY", "ANTIGRAVITY AI"},
            {"RENDER ENGINE", "CHRONON 3D V1.0"},
            {"CORE BACKEND", "BLEND2D + SIMD"},
            {"TELEMETRY", "DASHBOARD V2"},
            {"SPECIAL THANKS", "GOOGLE DEEPMIND"},
        };

        for (size_t i = 0; i < 6; ++i) {
            f32 item_z = scroll_z + static_cast<f32>(i) * 180.0f;
            s.layer("credit_" + std::to_string(i), [i, credits, item_z](LayerBuilder& l) {
                f32 dist_fade = 1.0f;
                if (item_z < -300.0f) {
                    dist_fade = std::max(0.0f, 1.0f - (-300.0f - item_z) / 200.0f);
                } else if (item_z > 1000.0f) {
                    dist_fade = std::max(0.0f, 1.0f - (item_z - 1000.0f) / 200.0f);
                }
                
                l.enable_3d()
                 .position({0.0f, -120.0f, item_z})
                 .opacity(dist_fade);

                apply_text(l, "role", {
                    .text = credits[i].role,
                    .size = 24.0f,
                    .color = {0.25f, 0.52f, 1.0f, 1.0f},
                    .align = TextAlign::Center,
                    .tracking = 8.0f,
                }, {W * 0.7f, 40.0f}, {0.0f, -30.0f, 0.0f});

                apply_text(l, "name", {
                    .text = credits[i].name,
                    .size = 36.0f,
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .align = TextAlign::Center,
                    .tracking = 2.0f,
                }, {W * 0.7f, 50.0f}, {0.0f, 20.0f, 0.0f});
            });
        }

        return s.build();
    });
}

} // namespace chronon3d::content::text
