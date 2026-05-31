#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

#include "helpers/text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

struct LayoutText {
    std::string text;
    f32 size{32.0f};
    Color color{1, 1, 1, 1};
    TextAlign align{TextAlign::Center};
    f32 tracking{0.0f};
    f32 line_height{1.2f};
    Vec3 pos{0, 0, 0};
    Vec2 box{W * 0.4f, 100.0f};

    LayoutText(std::string t) : text(std::move(t)) {}
    LayoutText& set_size(f32 s) { size = s; return *this; }
    LayoutText& set_color(Color c) { color = c; return *this; }
    LayoutText& set_align(TextAlign a) { align = a; return *this; }
    LayoutText& set_tracking(f32 t) { tracking = t; return *this; }
    LayoutText& set_line_height(f32 h) { line_height = h; return *this; }
    LayoutText& set_pos(Vec3 p) { pos = p; return *this; }
    LayoutText& set_box(Vec2 b) { box = b; return *this; }

    void draw(LayerBuilder& l, const std::string& name = "text") const {
        apply_text(l, name, TextDef{
            .text = text, .size = size, .color = color, .align = align,
            .tracking = tracking, .line_height = line_height
        }, box, pos);
    }
};

} // namespace

Composition text_split_screen() {
    return composition({.name = "TextSplitScreen", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 3.0f);

        s.layer("bg", [](auto& l) { l.fill({0.01f, 0.015f, 0.025f, 1.0f}); });
        s.layer("divider", [&](auto& l) {
            l.opacity(op).rect("div", {.size = {2.0f, H * 0.6f}, .color = {0.25f, 0.52f, 1, 0.2f}});
        });

        s.layer("left_col", [&](auto& l) {
            l.opacity(op);
            LayoutText("BEFORE").set_size(32).set_color({0.6f, 0.6f, 0.7f, 1}).set_tracking(6).set_pos({-W*0.25f, -160, 0}).draw(l, "title");
            LayoutText("Static compositions\nManual per-frame tuning\nRepeated boilerplate")
                .set_size(30).set_color({0.8f, 0.8f, 0.85f, 1}).set_line_height(1.6f).set_pos({-W*0.25f, -50, 0}).set_box({W*0.35f, 200}).draw(l, "body");
        });

        s.layer("right_col", [&](auto& l) {
            l.opacity(op);
            LayoutText("AFTER").set_size(32).set_color({0.25f, 0.52f, 1, 1}).set_tracking(6).set_pos({W*0.25f, -160, 0}).draw(l, "title");
            LayoutText("Procedural generation\nAnimated text pipelines\nReusable content assets")
                .set_size(30).set_line_height(1.6f).set_pos({W*0.25f, -50, 0}).set_box({W*0.35f, 200}).draw(l, "body");
        });
        return s.build();
    });
}

Composition text_multi_style() {
    return composition({.name = "TextMultiStyle", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 3.0f);

        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });
        s.layer("style_title", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::TopCenter, 60.0f);
            LayoutText("MULTI-STYLE COMPOSITION").set_size(44).set_color({0.6f, 0.6f, 0.8f, 1}).set_tracking(8).set_box({W*0.8f, 60}).draw(l);
        });

        auto row = [&](std::string name, LayoutText lt, f32 y) {
            s.layer(name, [&](auto& l) { l.opacity(op).pin_to(Anchor::Center).position({0, y, 0}); lt.draw(l); });
        };

        row("row1", LayoutText("Bold & Heavy").set_size(80).set_color({0.25f, 0.52f, 1, 1}).set_tracking(4).set_box({W*0.7f, 100}), -160);
        row("row2", LayoutText("WIDE  T R A C K I N G").set_size(48).set_color({0.9f, 0.6f, 0.2f, 1}).set_tracking(18).set_box({W*0.8f, 70}), -40);
        row("row3", LayoutText("Supporting detail in smaller size").set_size(32).set_color({0.7f, 0.7f, 0.8f, 1}).set_tracking(1.5f).set_box({W*0.6f, 50}), 60);
        row("row4", LayoutText("ACCENT HIGHLIGHT").set_size(26).set_color({0.3f, 1.0f, 0.6f, 1}).set_tracking(6).set_box({W*0.5f, 40}), 140);

        return s.build();
    });
}

Composition text_on_background() {
    return composition({.name = "TextOnBackground", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 3.0f);

        s.layer("bg", [](auto& l) { l.fullscreen_rect("bg", {0.02f, 0.02f, 0.06f, 1.0f}); });
        for (int i = 0; i < 5; ++i) {
            s.layer("deco_" + std::to_string(i), [&](auto& l) {
                l.opacity(op * 0.08f).position({0, H * 0.15f + i * H * 0.18f, 0}).rect("line", {.size = {W * 0.9f, 1.0f}});
            });
        }

        s.layer("main_text", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center);
            LayoutText("BACKGROUND TEXT").set_size(96).set_tracking(6).set_box({W*0.85f, 140}).draw(l);
        });

        s.layer("info", [&](auto& l) {
            l.opacity(op * 0.5f).pin_to(Anchor::BottomCenter, 40.0f);
            LayoutText("Chronon3D Content Pipeline   |   1920 x 1080   |   60fps").set_size(20).set_color({0.7f, 0.7f, 0.8f, 1}).set_tracking(3).set_box({W*0.6f, 30}).draw(l);
        });
        return s.build();
    });
}

Composition text_grid_overlay() {
    return composition({.name = "TextGridOverlay", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 3.0f);

        s.layer("bg", [](auto& l) { l.fill({0.005f, 0.008f, 0.020f, 1.0f}); });
        s.layer("grid", [&](auto& l) {
            l.opacity(op * 0.25f).grid_background("g", {.size={W,H}, .grid_color={0.18f, 0.5f, 0.96f, 1}, .spacing=84, .major_every=0});
        });

        s.layer("tech_title", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.text("title", TextParams{
                .text = "SYS::GRID_OVERLAY", .size = {W * 0.9f, 240.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf", .font_family = "Inter", .font_weight = 800,
                .font_size = 84.0f, .color = {0.24f, 0.58f, 1.0f, 1},
                .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = 6.0f,
            });
        });
        return s.build();
    });
}

} // namespace chronon3d::content::text
