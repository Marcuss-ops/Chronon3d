// content/text/text_fresh_compositions.cpp
//
// Fresh compositions: grid background + centered text + glow effects.
// Built after cleanup of legacy typewriter compositions.
//
// Compositions:
//   1. TextCenterTitle  — clean centered title on grid (no glow)
//   2. TextCenterGlow   — centered title with cinematic white glow
//   3. TextSubtitleGlow — title + subtitle, both with layered glow
//   4. TextBadgeGlow    — glowing badge with text on grid
//
// Render examples:
//   chronon3d_cli render TextCenterGlow --frames 90 -o output/TextCenterGlow.png

#include "content/text/text_theme.hpp"

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
//  TextCenterTitle — clean centered title on grid background
//  Fade-in title + subtitle, no glow. Clean and essential.
// ═════════════════════════════════════════════════════════════════════════════

Composition text_center_title() {
    return composition({.name = "TextCenterTitle", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        // Title — fades in smoothly
        const f32 title_op = fade_in(ctx.frame, 10.0f, 24.0f);
        s.layer("title", [title_op](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -30.0f, 0.0f})
             .opacity(title_op)
             .text("title", {
                 .text = "CHRONON3D",
                 .size = {1600.0f, 140.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 80.0f,
                 .color = TEXT_COLOR_WHITE,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 14.0f,
             });
        });

        // Subtitle — appears after a short delay
        const f32 sub_op = fade_in(ctx.frame, 34.0f, 20.0f);
        s.layer("subtitle", [sub_op](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 60.0f, 0.0f})
             .opacity(sub_op)
             .text("sub", {
                 .text = "motion · graphics · engine",
                 .size = {1000.0f, 50.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 26.0f,
                 .color = SUBTITLE_COLOR,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 8.0f,
             });
        });

        // Thin decorative line below subtitle
        const f32 line_op = sub_op * 0.5f;
        s.layer("divider", [line_op](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 95.0f, 0.0f})
             .opacity(line_op)
             .rect("line", {
                 .size = {80.0f, 2.0f},
                 .color = DIVIDER_COLOR,
             });
        });

        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
//  TextCenterGlow — centered title with layered cinematic glow + micro shadow
//  Uses TextGlowPresets::ae_cinematic_white() for professional multi-layer glow
// ═════════════════════════════════════════════════════════════════════════════

Composition text_center_glow() {
    return composition({.name = "TextCenterGlow", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        // Build the glow preset once
        auto glow = TextGlowPresets::ae_cinematic_white();
        glow.inner_radius    = 5.0f;
        glow.mid_radius      = 16.0f;
        glow.bloom_radius    = 38.0f;
        glow.inner_intensity = 0.60f;
        glow.mid_intensity   = 0.25f;
        glow.bloom_intensity = 0.10f;

        const f32 title_op = fade_in(ctx.frame, 8.0f, 22.0f);
        s.layer("hero", [title_op, glow](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -20.0f, 0.0f})
             .opacity(title_op)
             .glow(glow.to_glow_params());
            if (glow.micro_shadow) {
                l.drop_shadow(glow.micro_shadow_offset,
                              glow.micro_shadow_color,
                              glow.micro_shadow_radius);
            }
            l.text("hero", {
                .text = "GLOW TITLE",
                .size = {1600.0f, 140.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = TEXT_FONT_PATH,
                .font_size = 82.0f,
                .color = FRESH_TEXT_WHITE,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 10.0f,
            });
        });

        const f32 sub_op = fade_in(ctx.frame, 32.0f, 18.0f);
        s.layer("tagline", [sub_op](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 70.0f, 0.0f})
             .opacity(sub_op)
             .text("tag", {
                 .text = "cinematic text glow",
                 .size = {1000.0f, 40.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 22.0f,
                 .color = FRESH_TEXT_MUTED,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 6.0f,
             });
        });

        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
//  TextSubtitleGlow — title with cool blue glow + subtitle with warm gold glow
//  Two distinct glow styles layered on the same frame
// ═════════════════════════════════════════════════════════════════════════════

Composition text_subtitle_glow() {
    return composition({.name = "TextSubtitleGlow", .width = 1920, .height = 1080, .duration = 100}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        // Title — cool blue cinematic glow
        auto title_glow = TextGlowPresets::ae_cinematic_white();
        title_glow.inner_color     = FRESH_TEXT_WHITE;
        title_glow.outer_color     = FRESH_GLOW_BLUE;
        title_glow.inner_radius    = 4.0f;
        title_glow.mid_radius      = 14.0f;
        title_glow.bloom_radius    = 34.0f;
        title_glow.inner_intensity = 0.55f;
        title_glow.mid_intensity   = 0.22f;
        title_glow.bloom_intensity = 0.08f;

        const f32 title_op = fade_in(ctx.frame, 8.0f, 20.0f);
        s.layer("main_title", [title_op, title_glow](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -40.0f, 0.0f})
             .opacity(title_op)
             .glow(title_glow.to_glow_params());
            if (title_glow.micro_shadow) {
                l.drop_shadow(title_glow.micro_shadow_offset,
                              title_glow.micro_shadow_color,
                              title_glow.micro_shadow_radius);
            }
            l.text("title", {
                .text = "BLUE LIGHT",
                .size = {1600.0f, 130.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = TEXT_FONT_PATH,
                .font_size = 78.0f,
                .color = FRESH_TEXT_WHITE,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 12.0f,
            });
        });

        // Subtitle — warm gold glow
        auto sub_glow = TextGlowPresets::ae_cinematic_warm();
        sub_glow.inner_intensity = 0.50f;
        sub_glow.mid_intensity   = 0.20f;
        sub_glow.bloom_intensity = 0.07f;

        const f32 sub_op = fade_in(ctx.frame, 32.0f, 22.0f);
        s.layer("subtitle", [sub_op, sub_glow](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 85.0f, 0.0f})
             .opacity(sub_op)
             .glow(sub_glow.to_glow_params());
            l.text("sub", {
                .text = "warm gold glow",
                .size = {800.0f, 50.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = TEXT_FONT_PATH,
                .font_size = 28.0f,
                .color = FRESH_TEXT_WHITE,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 8.0f,
            });
        });

        // Accent orbs — subtle ambient glow behind the text
        s.layer("ambient_blue", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({-360.0f, -120.0f, 0.0f})
             .glow(GlowParams{
                 .enabled = true,
                 .radius = 220.0f,
                 .intensity = 0.35f,
                 .color = FRESH_GLOW_BLUE,
             });
            l.circle("orb", {
                .radius = 100.0f,
                .color = Color{0.05f, 0.10f, 0.30f, 0.20f},
            });
        });

        s.layer("ambient_gold", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({360.0f, 140.0f, 0.0f})
             .glow(GlowParams{
                 .enabled = true,
                 .radius = 180.0f,
                 .intensity = 0.30f,
                 .color = FRESH_GLOW_GOLD,
             });
            l.circle("orb", {
                .radius = 80.0f,
                .color = Color{0.30f, 0.15f, 0.05f, 0.15f},
            });
        });

        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
//  TextBadgeGlow — glowing badge with text inside, centered on grid
//  Cyan neon glow around a rounded badge with large text
// ═════════════════════════════════════════════════════════════════════════════

Composition text_badge_glow() {
    return composition({.name = "TextBadgeGlow", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        // Badge glow — neon cyan
        const GlowParams badge_glow = {
            .enabled = true,
            .radius = 48.0f,
            .intensity = 1.2f,
            .color = FRESH_GLOW_CYAN,
            .preserve_source = true,
            .additive = true,
        };

        const f32 badge_op = fade_in(ctx.frame, 10.0f, 20.0f);
        s.layer("badge", [badge_op, badge_glow](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -30.0f, 0.0f})
             .opacity(badge_op)
             .glow(badge_glow)
             .rounded_rect("bg", {
                 .size = {520.0f, 160.0f},
                 .radius = 32.0f,
                 .color = Color{0.05f, 0.08f, 0.14f, 0.92f},
             });
            // Thin stroked inner border using a path
            l.path("border", {
                .commands = {
                    PathCommand::move_to({-240.0f, -63.0f}),
                    PathCommand::line_to({240.0f, -63.0f}),
                    PathCommand::line_to({240.0f, 63.0f}),
                    PathCommand::line_to({-240.0f, 63.0f}),
                    PathCommand::close(),
                },
                .stroke = {.width = 1.2f, .cap = LineCap::Round, .join = LineJoin::Round},
                .fill = Fill::solid_color(Color{0, 0, 0, 0}),
                .color = Color{0.35f, 0.70f, 1.0f, 0.50f},
                .closed = true,
            });
            l.text("label", {
                .text = "CHRONON3D",
                .size = {480.0f, 100.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = TEXT_FONT_PATH,
                .font_size = 46.0f,
                .color = FRESH_TEXT_WHITE,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 10.0f,
            });
        });

        // Sub-label below badge
        const f32 sub_op = fade_in(ctx.frame, 34.0f, 18.0f);
        s.layer("sub_label", [sub_op](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 110.0f, 0.0f})
             .opacity(sub_op)
             .text("sub", {
                 .text = "neon badge · glow preset",
                 .size = {600.0f, 30.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 20.0f,
                 .color = FRESH_TEXT_MUTED,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 5.0f,
             });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
