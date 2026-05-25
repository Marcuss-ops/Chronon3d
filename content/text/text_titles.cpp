#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

#include "text_helpers.hpp"

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

} // namespace chronon3d::content::text
