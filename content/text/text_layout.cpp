#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

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

        s.layer("info", [&](LayerBuilder& l) {
            l.opacity(op * 0.5f).pin_to(Anchor::BottomCenter, 40.0f);
            apply_text(l, "info_text", {
                .text = "Chronon3D Content Pipeline   |   1920 x 1080   |   60fps",
                .size = 20.0f,
                .color = {0.7f, 0.7f, 0.8f, 1},
                .align = TextAlign::Center,
                .tracking = 3.0f,
            }, {W * 0.6f, 30.0f});
        });

        return s.build();
    });
}

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

} // namespace chronon3d::content::text
