#include "content/text/text_theme.hpp"

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// ──────────────────────────────────────────────────────────────────────────────
//  TextGridBackground — clean grid on dark background
// ──────────────────────────────────────────────────────────────────────────────

Composition text_grid_background() {
    return composition({.name = "TextGridBackground", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);
        return s.build();
    });
}

// ──────────────────────────────────────────────────────────────────────────────
//  TextHello — simple text on grid background, clean fade-in
// ──────────────────────────────────────────────────────────────────────────────

Composition text_hello() {
    return composition({.name = "TextHello", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        f32 title_op = fade_in(ctx.frame, 10.0f, 20.0f);
        s.layer("title", [title_op](auto& l) {
            l.pin_to(Anchor::Center)
             .opacity(title_op)
             .text("hello", {
                 .text = "HELLO, CHRONON3D.",
                 .size = {1500.0f, 120.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 64.0f,
                 .color = TEXT_COLOR_BLUE,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 6.0f,
             });
        });

        f32 sub_op = fade_in(ctx.frame, 30.0f, 20.0f);
        s.layer("subtitle", [sub_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 70.0f, 0.0f})
             .opacity(sub_op)
             .text("sub", {
                 .text = "grid + text = clean",
                 .size = {800.0f, 50.0f},
                 .pos = {0.0f, 0.0f, 0.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 28.0f,
                 .color = SUBTITLE_COLOR,
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
        add_text_background(s);

        f32 opacity = fade_in(ctx.frame, 10.0f, 20.0f);

        s.layer("image_card", [opacity](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -40.0f, 0.0f})
             .opacity(opacity);
            add_image_card(l, "assets/images/minimalist_landscape.png", {800.0f, 450.0f}, 12.0f);
        });

        f32 cap_op = fade_in(ctx.frame, 30.0f, 20.0f);
        s.layer("caption", [cap_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 290.0f, 0.0f})
             .opacity(cap_op)
             .text("cap", {
                 .text = "landscape — minimalist scene",
                 .size = {500.0f, 30.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 22.0f,
                 .color = CAPTION_COLOR,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
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
        add_text_background(s);

        f32 quote_op = fade_in(ctx.frame, 10.0f, 25.0f);
        f32 attr_op  = fade_in(ctx.frame, 45.0f, 20.0f);

        s.layer("quote_mark", [quote_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({-420.0f, -90.0f, 0.0f})
             .opacity(quote_op * 0.25f)
             .text("qm", {
                 .text = "\"",
                 .size = {120.0f, 180.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 160.0f,
                 .color = TEXT_COLOR_BLUE,
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
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 48.0f,
                 .color = TEXT_COLOR_WHITE,
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
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 22.0f,
                 .color = ATTR_COLOR,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
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
                 .color = DIVIDER_COLOR,
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
        add_text_background(s);

        f32 op = fade_in(ctx.frame, 10.0f, 20.0f);

        s.layer("title", [op](auto& l) {
            l.pin_to(Anchor::TopCenter, 50.0f)
             .opacity(op)
             .text("title", {
                 .text = "SHAPES",
                 .size = {500.0f, 60.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 42.0f,
                 .color = TITLE_COLOR,
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
            add_badge(l, badge_text, size, radius, bg_color, font_size);
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
        add_text_background(s);

        f32 op = fade_in(ctx.frame, 10.0f, 20.0f);

        s.layer("headline", [op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, -30.0f, 0.0f})
             .opacity(op)
             .text("hl", {
                 .text = "CHRONON3D",
                 .size = {1500.0f, 120.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 72.0f,
                 .color = TEXT_COLOR_BLUE,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 12.0f,
             });
        });

        f32 sub_op = fade_in(ctx.frame, 30.0f, 20.0f);
        s.layer("subtitle", [sub_op](auto& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 55.0f, 0.0f})
             .opacity(sub_op)
             .text("sub", {
                 .text = "motion graphics engine",
                 .size = {800.0f, 50.0f},
                 .font_path = TEXT_FONT_PATH,
                 .font_size = 26.0f,
                 .color = SUBTITLE_COLOR,
                 .align = TextAlign::Center,
                 .vertical_align = VerticalAlign::Middle,
                 .tracking = 6.0f,
             });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
