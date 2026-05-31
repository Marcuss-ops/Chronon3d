#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text {

Composition text_premium_hero() {
    return composition({.name = "TextPremiumHero", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto title_shadow = premium::shadow_style(
            {0.80f, 0.55f, 0.85f, 1.0f},
            {0.92f, 0.70f, 0.90f, 1.0f},
            {0.0f, 10.0f},
            24.0f,
            0.18f,
            {0.0f, 56.0f},
            160.0f,
            0.05f
        );
        const auto cta_shadow = premium::shadow_style(
            {0.88f, 0.66f, 0.78f, 1.0f},
            {0.96f, 0.82f, 0.92f, 1.0f},
            {0.0f, 12.0f},
            18.0f,
            0.24f,
            {0.0f, 36.0f},
            96.0f,
            0.08f
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
                        {1.0f, {0.98f, 0.96f, 1.0f, 1.0f}},
                    }
                )
            });
        });

        s.layer("corner_wedge_top_left", [](LayerBuilder& l) {
            l.rotate({0.0f, 0.0f, -46.0f});
            l.rect("wedge", {
                .size = {880.0f, 150.0f},
                .color = {0.92f, 0.82f, 1.0f, 0.55f},
                .pos = {-720.0f, -490.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.82f, 0.72f, 1.0f, 0.45f}},
                        {1.0f, {1.0f, 0.64f, 0.88f, 0.40f}},
                    }
                )
            });
        });

        s.layer("corner_wedge_bottom_left", [](LayerBuilder& l) {
            l.rotate({0.0f, 0.0f, -26.0f});
            l.rect("wedge", {
                .size = {1080.0f, 180.0f},
                .color = {0.88f, 0.78f, 1.0f, 0.50f},
                .pos = {-700.0f, 470.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.80f, 0.70f, 1.0f, 0.40f}},
                        {1.0f, {0.74f, 0.56f, 1.0f, 0.34f}},
                    }
                )
            });
        });

        s.layer("corner_circle", [](LayerBuilder& l) {
            l.opacity(0.26f);
            l.circle("big", {
                .radius = 215.0f,
                .color = {0.92f, 0.78f, 0.98f, 0.22f},
                .pos = {780.0f, 380.0f, 0.0f},
            });
        });

        s.layer("badge", [](LayerBuilder& l) {
            l.position({0.0f, -370.0f, 0.0f});
            l.rounded_rect("ae", {
                .size = {88.0f, 88.0f},
                .radius = 20.0f,
                .color = {0.09f, 0.09f, 0.28f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.08f, 0.08f, 0.30f, 1.0f}},
                        {1.0f, {0.05f, 0.05f, 0.16f, 1.0f}},
                    }
                )
            });
            l.text("ae_text", {
                .text = "Ae",
                .size = {88.0f, 88.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 40.0f,
                .color = {0.74f, 0.72f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .shadows = {
                    premium::shadow({0.0f, 0.0f}, 10.0f, 0.55f, {0.12f, 0.10f, 0.45f, 1.0f}),
                },
            });
        });

        s.layer("dot", [](LayerBuilder& l) {
            l.position({740.0f, -340.0f, 0.0f});
            l.circle("pink", {
                .radius = 36.0f,
                .color = {0.98f, 0.45f, 0.60f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
            l.circle("glow", {
                .radius = 52.0f,
                .color = {0.98f, 0.45f, 0.60f, 0.12f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
        });

        s.layer("hero", [=](LayerBuilder& l) {
            l.position({0.0f, -8.0f, 0.0f});
            l.text("title", premium::hero_text(
                "SaaS",
                {1000.0f, 220.0f},
                {0.0f, -58.0f, 0.0f},
                144.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 0.78f, 0.12f, 1.0f}},
                        {0.32f, {1.0f, 0.60f, 0.28f, 1.0f}},
                        {0.64f, {1.0f, 0.34f, 0.44f, 1.0f}},
                        {1.0f, {0.96f, 0.18f, 0.78f, 1.0f}},
                    }
                ),
                {1.0f, 1.0f, 1.0f, 0.08f},
                1.0f,
                -1.0f,
                VerticalAlign::Middle,
                title_shadow
            ));
            l.text("masterclass", premium::subtitle_text(
                "Masterclass",
                {800.0f, 110.0f},
                {0.0f, 122.0f, 0.0f},
                72.0f,
                {0.44f, 0.44f, 0.48f, 0.90f},
                0.0f,
                {
                    .contact = {
                        .enabled = true,
                        .offset = {0.0f, 6.0f},
                        .blur = 14.0f,
                        .opacity = 0.10f,
                        .color = {0.55f, 0.55f, 0.58f, 1.0f},
                    },
                    .ambient = {
                        .enabled = true,
                        .offset = {0.0f, 24.0f},
                        .blur = 64.0f,
                        .opacity = 0.04f,
                        .color = {0.72f, 0.70f, 0.78f, 1.0f},
                    },
                }
            ));
        });

        s.layer("cta", [=](LayerBuilder& l) {
            l.position({0.0f, 308.0f, 0.0f});
            l.drop_shadow({0.0f, 18.0f}, {1.0f, 0.72f, 0.26f, 0.22f}, 34.0f);
            l.rounded_rect("pill", {
                .size = {520.0f, 124.0f},
                .radius = 34.0f,
                .color = {1.0f, 0.58f, 0.30f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 0.72f, 0.18f, 1.0f}},
                        {0.48f, {1.0f, 0.48f, 0.40f, 1.0f}},
                        {1.0f, {1.0f, 0.22f, 0.60f, 1.0f}},
                    }
                )
            });
            l.text("part", {
                .text = "PART 1",
                .size = {520.0f, 124.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 48.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .paint = {
                    .fill = {1.0f, 1.0f, 1.0f, 1.0f},
                },
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

CHRONON_REGISTER_COMPOSITION("TextPremiumHero", chronon3d::content::text::text_premium_hero)
