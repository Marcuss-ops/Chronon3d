#include "typewriter_common.hpp"

namespace chronon3d::content::text {

// ──────────────────────────────────────────────────────────────────────────────
//  Helper — common grid background layer
// ──────────────────────────────────────────────────────────────────────────────

namespace {

void add_grid_background(SceneBuilder& s) {
    s.layer("bg", [](auto& l) {
        l.cache_static().grid_background("g", {
            .size = {1920.0f, 1080.0f},
            .bg_color = {0.01f, 0.012f, 0.022f, 1.0f},
            .grid_color = {0.18f, 0.5f, 0.96f, 0.12f},
            .spacing = 96.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.5f,
            .major_every = 4,
            .centered = true,
        });
    });
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
//  TextGridBackground — clean grid on dark background
// ──────────────────────────────────────────────────────────────────────────────

Composition text_grid_background() {
    return composition({.name = "TextGridBackground", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);
        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextHello — simple text on grid background, clean fade-in
// ──────────────────────────────────────────────────────────────────────────────

Composition text_hello() {
    return composition({.name = "TextHello", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);

        s.layer("title", [&ctx](auto& l) {
            f32 opacity = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 10.0f) / 20.0f, 0.0f, 1.0f);
            l.pin_to(Anchor::Center)
             .opacity(opacity)
             .text("hello", {
                 .text = "HELLO, CHRONON3D.",
                 .size = {1500.0f, 120.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_size = 64.0f,
                 .color = {0.25f, 0.58f, 1.0f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 6.0f,
             });
        });

        s.layer("subtitle", [&ctx](auto& l) {
            f32 opacity = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 30.0f) / 20.0f, 0.0f, 1.0f);
            l.pin_to(Anchor::Center)
             .position({0.0f, 70.0f, 0.0f})
             .opacity(opacity)
             .text("sub", {
                 .text = "grid + text = clean",
                 .size = {800.0f, 50.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_size = 28.0f,
                 .color = {0.78f, 0.82f, 0.9f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 1.0f,
             });
        });

        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  ImageOnGrid — image with rounded border on grid background
// ──────────────────────────────────────────────────────────────────────────────

Composition text_image_on_grid() {
    return composition({.name = "ImageOnGrid", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);

        f32 opacity = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 10.0f) / 20.0f, 0.0f, 1.0f);

        s.layer("image_card", [opacity](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -40.0f, 0.0f})
             .opacity(opacity);

            l.rounded_rect("shadow", {
                .size = {824.0f, 474.0f},
                .radius = 18.0f,
                .color = {0.0f, 0.0f, 0.0f, 0.35f},
                .pos = {0.0f, 6.0f, 0.0f},
            });

            l.rounded_rect("border", {
                .size = {802.0f, 452.0f},
                .radius = 14.0f,
                .color = {0.25f, 0.27f, 0.31f, 0.7f},
            });

            l.image("img", {
                .path = "assets/images/minimalist_landscape.png",
                .size = {800.0f, 450.0f},
                .radius = 12.0f,
            });
        });

        f32 cap_op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 30.0f) / 20.0f, 0.0f, 1.0f);
        s.layer("caption", [cap_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 290.0f, 0.0f})
             .opacity(cap_op)
             .text("cap", {
                 .text = "landscape — minimalist scene",
                 .size = {500.0f, 30.0f},
                 .font_size = 22.0f,
                 .color = {0.6f, 0.65f, 0.8f, 1.0f},
                 .align = TextAlign::Center,
                 .tracking = 3.0f,
             });
        });

        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  QuoteOnGrid — quote with attribution on grid background
// ──────────────────────────────────────────────────────────────────────────────

Composition text_quote_on_grid() {
    return composition({.name = "QuoteOnGrid", .width = 1920, .height = 1080, .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);

        f32 quote_op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 10.0f) / 25.0f, 0.0f, 1.0f);
        f32 attr_op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 45.0f) / 20.0f, 0.0f, 1.0f);

        s.layer("quote_mark", [quote_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({-420.0f, -90.0f, 0.0f})
             .opacity(quote_op * 0.25f)
             .text("qm", {
                 .text = "\"",
                 .size = {120.0f, 180.0f},
                 .font_size = 160.0f,
                 .color = {0.25f, 0.58f, 1.0f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
             });
        });

        s.layer("quote_text", [quote_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 0.0f, 0.0f})
             .opacity(quote_op)
             .text("qt", {
                 .text = "Design is not just what it looks like and feels on the surface — design is actually how the whole thing works together as a unified experience from the very first interaction to the very last detail, every single piece of the puzzle has to be carefully considered and crafted with purpose, intention, and love for the craft.",
                 .size = {1500.0f, 520.0f},
                 .font_size = 48.0f,
                 .color = {0.9f, 0.92f, 0.98f, 1.0f},
                 .align = TextAlign::Left,
                 .vertical_align = VerticalAlign::Middle,
                 .line_height = 1.3f,
                 .tracking = 1.0f,
                 .auto_fit = true,
                 .min_font_size = 18.0f,
                 .max_font_size = 48.0f,
             });
        });

        s.layer("attribution", [attr_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 160.0f, 0.0f})
             .opacity(attr_op)
             .text("attr", {
                 .text = "— Steve Jobs",
                 .size = {400.0f, 30.0f},
                 .font_size = 22.0f,
                 .color = {0.4f, 0.45f, 0.6f, 1.0f},
                 .align = TextAlign::Center,
                 .tracking = 2.0f,
             });
        });

        s.layer("divider", [attr_op](auto& l) {
            f32 line_op = attr_op * 0.4f;
            l.pin_to(Anchor::Center)
             .position({0.0f, 120.0f, 0.0f})
             .opacity(line_op)
             .rect("div", {
                 .size = {60.0f, 2.0f},
                 .color = {0.25f, 0.58f, 1.0f, 1.0f},
             });
        });

        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  ShapeOnGrid — shape badges with text inside on grid background
// ──────────────────────────────────────────────────────────────────────────────

Composition text_shape_on_grid() {
    return composition({.name = "ShapeOnGrid", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);

        f32 op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 10.0f) / 20.0f, 0.0f, 1.0f);

        s.layer("title", [op](auto& l) {
            l.pin_to(Anchor::TopCenter, 50.0f)
             .opacity(op)
             .text("title", {
                 .text = "SHAPES",
                 .size = {500.0f, 60.0f},
                 .font_size = 42.0f,
                 .color = {0.7f, 0.75f, 0.9f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 10.0f,
             });
        });

        auto draw_badge = [op](LayerBuilder& l, const std::string& badge_text, Vec3 pos,
                                Vec2 size, f32 radius, Color bg_color, f32 font_size) {
            l.pin_to(Anchor::Center)
             .position(pos)
             .opacity(op);
            l.rounded_rect("bg", {.size = size, .radius = radius, .color = bg_color});
            l.text("label", {
                .text = badge_text,
                .size = size,
                .font_size = font_size,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 8.0f,
            });
        };

        s.layer("rect_badge", [&](auto& l) {
            draw_badge(l, "RECT", {-500, -100, 0}, {300, 130}, 20, {0.20f, 0.50f, 0.90f, 0.95f}, 56);
        });
        s.layer("circle_badge", [&](auto& l) {
            draw_badge(l, "CIRCLE", {0, -100, 0}, {300, 130}, 65, {0.65f, 0.22f, 0.38f, 0.95f}, 52);
        });
        s.layer("rrect_badge", [&](auto& l) {
            draw_badge(l, "R-RECT", {500, -100, 0}, {300, 130}, 32, {0.20f, 0.72f, 0.44f, 0.95f}, 50);
        });
        s.layer("line_badge", [&](auto& l) {
            draw_badge(l, "LINE", {-500, 120, 0}, {300, 110}, 18, {0.06f, 0.08f, 0.16f, 0.85f}, 44);
        });
        s.layer("tinted_badge", [&](auto& l) {
            draw_badge(l, "TINTED", {0, 120, 0}, {300, 110}, 18, {0.90f, 0.42f, 0.0f, 0.95f}, 44);
        });
        s.layer("stroke_badge", [&](auto& l) {
            draw_badge(l, "STROKE", {500, 120, 0}, {300, 110}, 18, {0.05f, 0.08f, 0.20f, 0.85f}, 44);
        });

        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextBasic — minimal text on grid, clean and essential
// ──────────────────────────────────────────────────────────────────────────────

Composition text_basic() {
    return composition({.name = "TextBasic", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_grid_background(s);

        f32 op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 10.0f) / 20.0f, 0.0f, 1.0f);

        s.layer("headline", [op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -30.0f, 0.0f})
             .opacity(op)
             .text("hl", {
                 .text = "CHRONON3D",
                 .size = {1500.0f, 120.0f},
                 .font_size = 72.0f,
                 .color = {0.25f, 0.58f, 1.0f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 12.0f,
             });
        });

        f32 sub_op = std::clamp((static_cast<f32>(ctx.frame.frame.frame) - 30.0f) / 20.0f, 0.0f, 1.0f);
        s.layer("subtitle", [sub_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 55.0f, 0.0f})
             .opacity(sub_op)
             .text("sub", {
                 .text = "motion graphics engine",
                 .size = {800.0f, 50.0f},
                 .font_size = 26.0f,
                 .color = {0.7f, 0.75f, 0.88f, 1.0f},
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 6.0f,
             });
        });

        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextTypewriter — classic typewriter on grid (centered, wrapping)
// ──────────────────────────────────────────────────────────────────────────────

Composition text_typewriter() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextTypewriter", {
        TypewriterLine("THE ENGINE LEARNED TO SPEAK, typed frame by frame — a single line that wraps when it reaches the edge of the viewport so you can see the centered alignment in action on multiple rows.")
            .set_pos({0.0f, 0.0f, 0})
            .set_font(52, 4)
            .set_timing(0, 1000.0f)
            .set_color({0.62f, 0.88f, 1.0f, 1.0f})
            .set_align(TextAlign::Center)
            .set_size({1500.0f, 600.0f})
            .set_cursor(false)
    },
    presets::motion::MotionPreset::FadeIn,
    false,
    {0.01f, 0.012f, 0.022f, 1.0f},
    30,
    1100.0f, 1920, 1080,
    [](f32) {
        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {0.0f, 0.0f, -1000.0f};
        cam.zoom = 1100.0f;
        return cam;
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextSweepReveal — dramatic perspective sweep with camera motion
// ──────────────────────────────────────────────────────────────────────────────

Composition text_sweep_reveal() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextSweepReveal", {
        TypewriterLine("A SINGLE TYPEWRITER LINE SWEEPS INTO VIEW with perspective camera motion and a dramatic reveal that pushes the text from deep space toward the viewer.")
            .set_pos({0, 0, 0})
            .set_font(42, 3)
            .set_timing(0, 1.5f)
            .set_color({0.25f, 0.58f, 1, 1})
            .set_align(TextAlign::Left)
            .set_size({1400.0f, 300.0f})
    }, presets::motion::MotionPreset::PerspectiveSweepTextReveal, false, {0.01f, 0.012f, 0.022f, 1.0f}, 180, 1380.0f, 1920, 1080);
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextStaggerReveal — letters stagger in one by one with subtle movement
// ──────────────────────────────────────────────────────────────────────────────

Composition text_stagger_reveal() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextStaggerReveal", {
        TypewriterLine("EACH LETTER STAGGERS INTO VIEW one character at a time with a gentle lift and settle — a clean animated reveal for a single longer sentence that tests both the stagger timing and the wrapping layout on multiple rows.")
            .set_pos({0, 0, 0})
            .set_font(36, 2)
            .set_timing(0, 2.0f)
            .set_color({0.25f, 0.58f, 1, 1})
            .set_align(TextAlign::Left)
            .set_size({1400.0f, 360.0f})
    }, presets::motion::MotionPreset::StaggerReveal, false, {0.01f, 0.012f, 0.022f, 1.0f}, 220, 1200.0f, 1920, 1080);
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextGlowReveal — soft glow bloom with typewriter reveal
// ──────────────────────────────────────────────────────────────────────────────

Composition text_glow_reveal() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextGlowReveal", {
        TypewriterLine("A GLOWING TYPEWRITER LINE BLOOMS ON SCREEN with a soft halo effect that makes each character pulse gently as it appears — perfect for dramatic and atmospheric typography motion.")
            .set_pos({0, 0, 0})
            .set_font(40, 3)
            .set_timing(0, 1.8f)
            .set_color({0.90f, 0.92f, 1.0f, 1})
            .set_align(TextAlign::Left)
            .set_size({1400.0f, 280.0f})
    }, presets::motion::MotionPreset::GlowBloom, false, {0.01f, 0.012f, 0.022f, 1.0f}, 180, 1100.0f, 1920, 1080);
}

} // namespace chronon3d::content::text
