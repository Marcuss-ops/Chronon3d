#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text {

Composition text_premium_hero_explainer() {
    return composition({.name = "TextPremiumHeroExplainer", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto hero_shadow = premium::shadow_style(
            {0.92f, 0.52f, 0.78f, 1.0f},
            {0.96f, 0.76f, 0.92f, 1.0f},
            {0.0f, 10.0f},
            20.0f,
            0.16f,
            {0.0f, 54.0f},
            150.0f,
            0.04f
        );
        const auto card_shadow = premium::shadow_style(
            {0.88f, 0.86f, 0.96f, 1.0f},
            {0.94f, 0.80f, 0.94f, 1.0f},
            {0.0f, 14.0f},
            24.0f,
            0.18f,
            {0.0f, 52.0f},
            120.0f,
            0.05f
        );
        const auto subtitle_shadow = premium::shadow_style(
            {0.90f, 0.52f, 0.76f, 1.0f},
            {0.96f, 0.74f, 0.90f, 1.0f},
            {0.0f, 6.0f},
            12.0f,
            0.12f,
            {0.0f, 26.0f},
            88.0f,
            0.03f
        );

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("paper", {
                .size = {W, H},
                .color = {0.995f, 0.993f, 0.998f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                        {1.0f, {0.99f, 0.96f, 1.0f, 1.0f}},
                    }
                )
            });
        });

        s.layer("top_left_wedge", [](LayerBuilder& l) {
            l.rotate({0.0f, 0.0f, -43.0f});
            l.rect("wedge", {
                .size = {860.0f, 160.0f},
                .color = {0.94f, 0.84f, 1.0f, 0.42f},
                .pos = {-770.0f, -485.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.90f, 0.82f, 1.0f, 0.38f}},
                        {1.0f, {0.98f, 0.72f, 0.96f, 0.32f}},
                    }
                )
            });
        });

        s.layer("side_orb_left", [](LayerBuilder& l) {
            l.opacity(0.20f);
            l.circle("orb", {
                .radius = 180.0f,
                .color = {0.96f, 0.80f, 0.96f, 0.20f},
                .pos = {-820.0f, -260.0f, 0.0f},
            });
        });

        s.layer("side_orb_right", [](LayerBuilder& l) {
            l.opacity(0.18f);
            l.circle("orb", {
                .radius = 160.0f,
                .color = {0.90f, 0.76f, 1.0f, 0.18f},
                .pos = {820.0f, -250.0f, 0.0f},
            });
        });

        s.layer("title", [=](LayerBuilder& l) {
            l.position({0.0f, -336.0f, 0.0f});
            l.text("main", premium::hero_text(
                "SaaS Explainer",
                {1360.0f, 150.0f},
                {0.0f, 0.0f, 0.0f},
                112.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 0.30f, 0.72f, 1.0f}},
                        {0.50f, {1.0f, 0.54f, 0.78f, 1.0f}},
                        {1.0f, {0.95f, 0.48f, 0.96f, 1.0f}},
                    }
                ),
                {1.0f, 1.0f, 1.0f, 0.05f},
                1.2f,
                -1.0f,
                VerticalAlign::Middle,
                hero_shadow
            ));
            l.text("badge", {
                .text = "masterclass",
                .size = {246.0f, 56.0f},
                .pos = {520.0f, 48.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 22.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {
                    .enabled = true,
                    .padding = {16.0f, 10.0f},
                    .radius = 20.0f,
                    .background = {1.0f, 0.34f, 0.70f, 0.92f},
                },
                .shadows = {
                    premium::shadow({0.0f, 8.0f}, 18.0f, 0.18f, {1.0f, 0.45f, 0.70f, 1.0f}),
                    premium::shadow({0.0f, 28.0f}, 44.0f, 0.08f, {1.0f, 0.45f, 0.70f, 1.0f}),
                },
            });
        });

        s.layer("deck", [=](LayerBuilder& l) {
            l.position({0.0f, 158.0f, 0.0f});
            l.rounded_rect("back", {
                .size = {1360.0f, 720.0f},
                .radius = 36.0f,
                .color = {0.96f, 0.95f, 0.99f, 0.82f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 0.86f}},
                        {1.0f, {0.95f, 0.95f, 1.0f, 0.78f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 24.0f}, {0.0f, 0.0f, 0.0f, 0.10f}, 58.0f);
        });

        s.layer("cards", [=](LayerBuilder& l) {
            l.position({0.0f, 190.0f, 0.0f});

            l.rounded_rect("left", {
                .size = {300.0f, 320.0f},
                .radius = 30.0f,
                .color = {1.0f, 1.0f, 1.0f, 0.95f},
                .pos = {-380.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 0.97f}},
                        {1.0f, {0.97f, 0.97f, 1.0f, 0.92f}},
                    }
                )
            });

            l.rounded_rect("center", {
                .size = {460.0f, 380.0f},
                .radius = 34.0f,
                .color = {1.0f, 1.0f, 1.0f, 0.96f},
                .pos = {0.0f, -10.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                        {1.0f, {0.96f, 0.96f, 1.0f, 0.94f}},
                    }
                )
            });

            l.rounded_rect("right", {
                .size = {280.0f, 310.0f},
                .radius = 28.0f,
                .color = {1.0f, 1.0f, 1.0f, 0.93f},
                .pos = {390.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 0.97f}},
                        {1.0f, {0.96f, 0.96f, 1.0f, 0.90f}},
                    }
                )
            });
        });

        s.layer("card_titles", [=](LayerBuilder& l) {
            l.position({0.0f, 80.0f, 0.0f});
            l.text("quick", premium::subtitle_text(
                "Quick Notes",
                {330.0f, 60.0f},
                {-350.0f, -130.0f, 0.0f},
                36.0f,
                {0.96f, 0.50f, 0.80f, 1.0f},
                0.4f,
                subtitle_shadow
            ));
            l.text("notes", premium::subtitle_text(
                "documents",
                {330.0f, 60.0f},
                {390.0f, -130.0f, 0.0f},
                32.0f,
                {0.96f, 0.50f, 0.80f, 0.90f},
                0.4f,
                subtitle_shadow
            ));
        });

        s.layer("notes", [=](LayerBuilder& l) {
            l.position({0.0f, 202.0f, 0.0f});
            l.rounded_rect("note", {
                .size = {410.0f, 500.0f},
                .radius = 30.0f,
                .color = {1.0f, 1.0f, 1.0f, 0.98f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                        {1.0f, {0.97f, 0.97f, 1.0f, 0.96f}},
                    }
                )
            });
            l.text("body", {
                .text = "Every idea starts small. A place for creators, writers and thinkers to shape notes, scripts and systems into something publishable.",
                .size = {332.0f, 360.0f},
                .pos = {0.0f, 18.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_weight = 400,
                .font_style = "normal",
                .font_size = 18.0f,
                .color = {0.42f, 0.44f, 0.52f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.12f,
                .tracking = 0.1f,
                .max_lines = 9,
                .wrap = TextWrap::Word,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

CHRONON_REGISTER_COMPOSITION("TextPremiumHeroExplainer", chronon3d::content::text::text_premium_hero_explainer)
