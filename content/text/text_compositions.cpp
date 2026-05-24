#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

namespace chronon3d::content::text {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────────

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

struct TextDef {
    std::string  text;
    f32          size{72.0f};
    Color        color{1, 1, 1, 1};
    TextAlign    align{TextAlign::Center};
    f32          tracking{0.0f};
    f32          line_height{1.2f};
};

struct StyleLayer {
    TextDef      def;
    f32          y_pos{0.0f};
    Vec2         size{W * 0.7f, 160.0f};
};

// Apply common text settings to a LayerBuilder
void apply_text(LayerBuilder& l, const std::string& name, const TextDef& d,
                Vec2 sz = {W * 0.7f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f}) {
    l.text(name, TextParams{
        .text = d.text,
        .size = sz,
        .pos = pos,
        .font_family = "Inter",
        .font_weight = 800,
        .font_style = "normal",
        .font_size = d.size,
        .color = d.color,
        .align = d.align,
        .line_height = d.line_height,
        .tracking = d.tracking,
    });
}

// ── TextTitleBig ─────────────────────────────────────────────────────────
// Large centered title with subtle fade-in progression through duration
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

// ── TextSubtitle ─────────────────────────────────────────────────────────
// Bottom-aligned subtitle for broadcast-style lower text
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

// ── TextLowerThird ───────────────────────────────────────────────────────
// Classic lower-third: name bar on left + title text
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

// ── TextCreditRoll ───────────────────────────────────────────────────────
// Scrolling credits from bottom to top
Composition text_credit_roll() {
    return composition({
        .name = "TextCreditRoll",
        .width = 1920, .height = 1080,
        .duration = 300,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 scroll_y = p * 1200.0f + H;

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.008f, 0.020f, 1.0f});
        });

        // Title
        s.layer("credits_title", [&](LayerBuilder& l) {
            l.position({0, scroll_y - 200, 0});
            apply_text(l, "title", {
                .text = "C R E D I T S",
                .size = 56.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 14.0f,
            }, {W * 0.6f, 80.0f}, {0, 0, 0});
        });

        // Role entries
        struct Credit { const char* role; const char* name; };
        const Credit credits[] = {
            {"Direction", "Pierone"},
            {"Engine", "Chronon3D Team"},
            {"Rendering", "Software Backend"},
            {"Animation", "Motion Presets"},
            {"Typography", "Inter Typeface"},
            {"Pipeline", "Content Generator"},
            {"Testing", "Automated Suite"},
            {"Tools", "CLI Application"},
        };

        for (size_t i = 0; i < 8; ++i) {
            f32 cy = scroll_y - 320.0f - static_cast<f32>(i) * 110.0f;
            s.layer("role_" + std::to_string(i), [&, i, cy](LayerBuilder& l) {
                l.position({0, cy, 0});
                apply_text(l, "role_text", {
                    .text = credits[i].role,
                    .size = 28.0f,
                    .color = {0.6f, 0.6f, 0.7f, 1},
                    .align = TextAlign::Center,
                    .tracking = 4.0f,
                }, {W * 0.4f, 40.0f}, {-200, 0, 0});

                apply_text(l, "name_text", {
                    .text = credits[i].name,
                    .size = 36.0f,
                    .color = {1, 1, 1, 1},
                    .align = TextAlign::Center,
                    .tracking = 1.0f,
                }, {W * 0.4f, 40.0f}, {200, 0, 0});
            });
        }

        return s.build();
    });
}

// ── TextSplitScreen ──────────────────────────────────────────────────────
// Two-column text comparison
Composition text_split_screen() {
    return composition({
        .name = "TextSplitScreen",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.015f, 0.025f, 1.0f});
        });

        s.layer("divider", [&](LayerBuilder& l) {
            l.opacity(op).rect("div", {
                .size = {2.0f, H * 0.6f},
                .color = {0.25f, 0.52f, 1.0f, 0.2f},
            });
        });

        // Left column
        s.layer("left_col", [&](LayerBuilder& l) {
            l.opacity(op);
            apply_text(l, "left_title", {
                .text = "BEFORE",
                .size = 32.0f,
                .color = {0.6f, 0.6f, 0.7f, 1},
                .align = TextAlign::Center,
                .tracking = 6.0f,
            }, {W * 0.4f, 50.0f}, {-W * 0.25f, -160, 0});

            apply_text(l, "left_body", {
                .text = "Static compositions\nManual per-frame tuning\nRepeated boilerplate",
                .size = 30.0f,
                .color = {0.8f, 0.8f, 0.85f, 1},
                .align = TextAlign::Center,
                .line_height = 1.6f,
            }, {W * 0.35f, 200.0f}, {-W * 0.25f, -50, 0});
        });

        // Right column
        s.layer("right_col", [&](LayerBuilder& l) {
            l.opacity(op);
            apply_text(l, "right_title", {
                .text = "AFTER",
                .size = 32.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 6.0f,
            }, {W * 0.4f, 50.0f}, {W * 0.25f, -160, 0});

            apply_text(l, "right_body", {
                .text = "Procedural generation\nAnimated text pipelines\nReusable content assets",
                .size = 30.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .line_height = 1.6f,
            }, {W * 0.35f, 200.0f}, {W * 0.25f, -50, 0});
        });

        return s.build();
    });
}

// ── TextGlow ─────────────────────────────────────────────────────────────
// Large text with glow effect
Composition text_glow() {
    return composition({
        .name = "TextGlow",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pulse = 0.6f + 0.4f * std::sin(p * 3.14159f * 4.0f);
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.005f, 0.02f, 1.0f});
        });

        s.layer("glow_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.glow(40.0f * pulse, pulse, Color{0.25f, 0.52f, 1.0f});
            apply_text(l, "text", {
                .text = "GLOW EFFECT",
                .size = 110.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 8.0f,
            }, {W * 0.8f, 160.0f});
        });

        return s.build();
    });
}

// ── TextShadow ───────────────────────────────────────────────────────────
// Text with drop shadow
Composition text_shadow() {
    return composition({
        .name = "TextShadow",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.05f, 0.05f, 0.06f, 1.0f});
        });

        s.layer("shadow_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.drop_shadow({6.0f, 10.0f}, {0, 0, 0, 0.55f}, 16.0f);
            apply_text(l, "text", {
                .text = "DROP SHADOW",
                .size = 100.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 5.0f,
            }, {W * 0.8f, 140.0f});
        });

        return s.build();
    });
}

// ── TextPulse ────────────────────────────────────────────────────────────
// Text with opacity pulse animation
Composition text_pulse() {
    return composition({
        .name = "TextPulse",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pulse = 0.5f + 0.5f * std::sin(p * 6.2832f * 2.0f);
        f32 scale = 1.0f + 0.05f * std::sin(p * 6.2832f * 2.0f);
        f32 op = std::min(1.0f, p * 4.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.010f, 0.025f, 1.0f});
        });

        s.layer("pulse_text", [&](LayerBuilder& l) {
            l.opacity(op * pulse)
             .pin_to(Anchor::Center)
             .scale({scale, scale, 1});
            apply_text(l, "text", {
                .text = "PULSING",
                .size = 100.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 10.0f,
            }, {W * 0.8f, 140.0f});
        });

        s.layer("sub_pulse", [&](LayerBuilder& l) {
            l.opacity(op * (1.0f - pulse) * 0.6f)
             .pin_to(Anchor::Center);
            apply_text(l, "sub", {
                .text = "Pulse animation via opacity & scale",
                .size = 28.0f,
                .color = {0.8f, 0.8f, 0.85f, 1},
                .align = TextAlign::Center,
                .tracking = 2.0f,
            }, {W * 0.6f, 50.0f}, {0, 100, 0});
        });

        return s.build();
    });
}

// ── TextMultiStyle ───────────────────────────────────────────────────────
// Mix of fonts, sizes, colors in one composition
Composition text_multi_style() {
    return composition({
        .name = "TextMultiStyle",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.02f, 0.02f, 0.04f, 1.0f});
        });

        // Title — large, light weight
        s.layer("style_title", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::TopCenter, 60.0f);
            apply_text(l, "title", {
                .text = "MULTI-STYLE COMPOSITION",
                .size = 44.0f,
                .color = {0.6f, 0.6f, 0.8f, 1},
                .align = TextAlign::Center,
                .tracking = 8.0f,
            }, {W * 0.8f, 60.0f});
        });

        // Row 1: Big bold heavy text
        s.layer("row1", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({0, -160, 0});
            apply_text(l, "r1", {
                .text = "Bold & Heavy",
                .size = 80.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 4.0f,
            }, {W * 0.7f, 100.0f});
        });

        // Row 2: medium, wider tracking, different color
        s.layer("row2", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({0, -40, 0});
            apply_text(l, "r2", {
                .text = "WIDE  T R A C K I N G",
                .size = 48.0f,
                .color = {0.9f, 0.6f, 0.2f, 1},
                .align = TextAlign::Center,
                .tracking = 18.0f,
            }, {W * 0.8f, 70.0f});
        });

        // Row 3: smaller, light color
        s.layer("row3", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({0, 60, 0});
            apply_text(l, "r3", {
                .text = "Supporting detail in smaller size",
                .size = 32.0f,
                .color = {0.7f, 0.7f, 0.8f, 1},
                .align = TextAlign::Center,
                .tracking = 1.5f,
            }, {W * 0.6f, 50.0f});
        });

        // Row 4: accent highlight
        s.layer("row4", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({0, 140, 0});
            apply_text(l, "r4", {
                .text = "ACCENT HIGHLIGHT",
                .size = 26.0f,
                .color = {0.3f, 1.0f, 0.6f, 1},
                .align = TextAlign::Center,
                .tracking = 6.0f,
            }, {W * 0.5f, 40.0f});
        });

        return s.build();
    });
}

// ── TextOnBackground ─────────────────────────────────────────────────────
// Text over a procedural gradient background
Composition text_on_background() {
    return composition({
        .name = "TextOnBackground",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.06f, 1.0f});
        });

        // Decorative horizontal lines
        for (int i = 0; i < 5; ++i) {
            f32 y = H * 0.15f + static_cast<f32>(i) * H * 0.18f;
            s.layer("deco_" + std::to_string(i), [&, y, op](LayerBuilder& l) {
                l.opacity(op * 0.08f).position({0, y, 0});
                l.rect("line", {
                    .size = {W * 0.9f, 1.0f},
                    .color = {1, 1, 1, 1},
                });
            });
        }

        // Main text
        s.layer("main_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            apply_text(l, "main", {
                .text = "BACKGROUND TEXT",
                .size = 96.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 6.0f,
            }, {W * 0.85f, 140.0f});
        });

        // Bottom info text
        s.layer("info", [&](LayerBuilder& l) {
            l.opacity(op * 0.5f).pin_to(Anchor::BottomCenter, 40.0f);
            apply_text(l, "info_text", {
                .text = "Chronon3D Content Pipeline   |   1920 × 1080   |   60fps",
                .size = 20.0f,
                .color = {0.7f, 0.7f, 0.8f, 1},
                .align = TextAlign::Center,
                .tracking = 3.0f,
            }, {W * 0.6f, 30.0f});
        });

        return s.build();
    });
}

// ── TextGridOverlay ──────────────────────────────────────────────────────
// Tech-style text with grid overlay
Composition text_grid_overlay() {
    return composition({
        .name = "TextGridOverlay",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.008f, 0.020f, 1.0f});
        });

        // Grid overlay layer
        s.layer("grid", [&](LayerBuilder& l) {
            l.opacity(op * 0.25f);
            l.grid_background("grid_bg", {
                .size = {W, H},
                .bg_color = {0, 0, 0, 0},
                .grid_color = {0.25f, 0.52f, 1.0f, 1},
                .spacing = 80.0f,
                .major_every = 4,
            });
        });

        // Tech title
        s.layer("tech_title", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({0, -60, 0});
            apply_text(l, "title", {
                .text = "SYS::GRID_OVERLAY",
                .size = 72.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 10.0f,
            }, {W * 0.85f, 100.0f});
        });

        // Sub-label
        s.layer("tech_sub", [&](LayerBuilder& l) {
            l.opacity(op * 0.6f).pin_to(Anchor::Center).position({0, 60, 0});
            apply_text(l, "sub", {
                .text = "> content_generation_pipeline  --grid  --tech-style",
                .size = 22.0f,
                .color = {0.5f, 0.8f, 0.5f, 1},
                .align = TextAlign::Center,
                .tracking = 1.0f,
            }, {W * 0.7f, 30.0f});
        });

        return s.build();
    });
}

// ── TextTypewriter ───────────────────────────────────────────────────────
// Character-by-character reveal simulation
Composition text_typewriter() {
    return composition({
        .name = "TextTypewriter",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const std::string full_text =
            "In the beginning, the engine\n"
            "rendered only silence.\n\n"
            "Then came the text pipeline,\n"
            "and with it, the word.";

        f32 p = ctx.progress();
        f32 chars_per_frame = 3.0f;
        size_t visible = std::min(full_text.size(),
            static_cast<size_t>(static_cast<f32>(ctx.frame) * chars_per_frame));
        std::string shown = full_text.substr(0, visible);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.012f, 0.022f, 1.0f});
        });

        f32 cursor_op = 0.5f + 0.5f * std::sin(p * 3.14159f * 8.0f);
        std::string display = shown + (visible < full_text.size()
            ? std::string(1, static_cast<char>(0xE2)) +
              std::string(1, static_cast<char>(0x96)) +
              std::string(1, static_cast<char>(0x88))
            : "");

        s.layer("typewriter", [&](LayerBuilder& l) {
            l.opacity(std::min(1.0f, p * 4.0f))
             .position({-W * 0.35f, 0, 0});
            apply_text(l, "text", {
                .text = display,
                .size = 38.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .tracking = 1.0f,
                .line_height = 1.5f,
            }, {W * 0.7f, 300.0f}, {0, 0, 0});
        });

        return s.build();
    });
}

// ── TextAnimatedSequence ─────────────────────────────────────────────────
// Multi-part text with timed sequence: fade-in → hold → fade-out → next
Composition text_animated_sequence() {
    return composition({
        .name = "TextAnimatedSequence",
        .width = 1920, .height = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 frame = static_cast<f32>(ctx.frame);

        struct SeqItem {
            std::string text;
            f32 start_frame;
            f32 dur;
            f32 size{72.0f};
        };
        const SeqItem items[] = {
            {"PART ONE",          0.0f,  50.0f, 80.0f},
            {"The Journey Begins", 55.0f, 50.0f, 56.0f},
            {"PART TWO",          110.0f, 55.0f, 80.0f},
            {"Resolution",        170.0f, 10.0f, 56.0f},
        };

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.012f, 0.025f, 1.0f});
        });

        for (const auto& item : items) {
            f32 local = frame - item.start_frame;
            f32 fade_in = 10.0f;
            f32 fade_out = 10.0f;
            f32 hold = item.dur - fade_in - fade_out;

            f32 opacity = 0.0f;
            if (local >= 0 && local < fade_in) {
                opacity = local / fade_in;
            } else if (local >= fade_in && local < fade_in + hold) {
                opacity = 1.0f;
            } else if (local >= fade_in + hold && local < item.dur) {
                opacity = 1.0f - (local - fade_in - hold) / fade_out;
            }

            if (opacity > 0.0f) {
                s.layer("seq_" + item.text, [&, opacity, item](LayerBuilder& l) {
                    l.opacity(opacity).pin_to(Anchor::Center);
                    apply_text(l, "text", {
                        .text = item.text,
                        .size = item.size,
                        .color = {1, 1, 1, 1},
                        .align = TextAlign::Center,
                        .tracking = 6.0f,
                    }, {W * 0.8f, 140.0f});
                });
            }
        }

        return s.build();
    });
}

// ── TextCountdown ────────────────────────────────────────────────────────
// Large countdown number animation
Composition text_countdown() {
    return composition({
        .name = "TextCountdown",
        .width = 1920, .height = 1080,
        .duration = 30,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 frame = static_cast<f32>(ctx.frame);
        f32 count = 3.0f - frame / 10.0f;
        i32 num = std::max(1, static_cast<i32>(std::ceil(count)));
        f32 flicker = 0.9f + 0.1f * std::sin(frame * 0.5f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.02f, 0.02f, 0.04f, 1.0f});
        });

        s.layer("count_num", [&](LayerBuilder& l) {
            l.opacity(flicker).pin_to(Anchor::Center);
            apply_text(l, "num", {
                .text = std::to_string(num),
                .size = 220.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 0.0f,
            }, {W * 0.5f, 300.0f});
        });

        return s.build();
    });
}

} // anonymous namespace

// ── Registration ─────────────────────────────────────────────────────────
void register_all() {
    // Static init handles registration via CHRONON_REGISTER_COMPOSITION macros
}

} // namespace chronon3d::content::text

// ── Macro Declarations ───────────────────────────────────────────────────
CHRONON_REGISTER_COMPOSITION("TextTitleBig",        chronon3d::content::text::text_title_big)
CHRONON_REGISTER_COMPOSITION("TextSubtitle",        chronon3d::content::text::text_subtitle)
CHRONON_REGISTER_COMPOSITION("TextLowerThird",      chronon3d::content::text::text_lower_third)
CHRONON_REGISTER_COMPOSITION("TextCreditRoll",      chronon3d::content::text::text_credit_roll)
CHRONON_REGISTER_COMPOSITION("TextSplitScreen",     chronon3d::content::text::text_split_screen)
CHRONON_REGISTER_COMPOSITION("TextGlow",            chronon3d::content::text::text_glow)
CHRONON_REGISTER_COMPOSITION("TextShadow",          chronon3d::content::text::text_shadow)
CHRONON_REGISTER_COMPOSITION("TextPulse",           chronon3d::content::text::text_pulse)
CHRONON_REGISTER_COMPOSITION("TextMultiStyle",      chronon3d::content::text::text_multi_style)
CHRONON_REGISTER_COMPOSITION("TextOnBackground",    chronon3d::content::text::text_on_background)
CHRONON_REGISTER_COMPOSITION("TextGridOverlay",     chronon3d::content::text::text_grid_overlay)
CHRONON_REGISTER_COMPOSITION("TextTypewriter",      chronon3d::content::text::text_typewriter)
CHRONON_REGISTER_COMPOSITION("TextAnimatedSequence",chronon3d::content::text::text_animated_sequence)
CHRONON_REGISTER_COMPOSITION("TextCountdown",       chronon3d::content::text::text_countdown)
