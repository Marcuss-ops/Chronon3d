#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text {

Composition text_premium_hero_saas_blue() {
    return composition({.name = "TextPremiumHeroSaaSBlue", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto hero_shadow = premium::shadow_style(
            {0.03f, 0.14f, 0.44f, 1.0f},
            {0.06f, 0.36f, 0.88f, 1.0f},
            {0.0f, 8.0f},
            18.0f,
            0.28f,
            {0.0f, 48.0f},
            150.0f,
            0.06f
        );
        const auto subtitle_shadow = premium::shadow_style(
            {0.12f, 0.28f, 0.82f, 1.0f},
            {0.20f, 0.56f, 1.0f, 1.0f},
            {0.0f, 4.0f},
            10.0f,
            0.16f,
            {0.0f, 22.0f},
            86.0f,
            0.04f
        );

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("blue", {
                .size = {W, H},
                .color = {0.01f, 0.06f, 0.24f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.04f, 0.18f, 0.60f, 1.0f}},
                        {0.50f, {0.02f, 0.32f, 0.86f, 1.0f}},
                        {1.0f, {0.00f, 0.06f, 0.20f, 1.0f}},
                    }
                )
            });
        });

        premium::add_ambient_blob(s, "top_glow_soft", {0.0f, -420.0f, 0.0f}, 420.0f, {0.12f, 0.76f, 1.0f, 0.14f}, 240.0f, 0.72f);
        premium::add_soft_orb(s, "top_glow", {0.0f, -380.0f, 0.0f}, 300.0f, {0.18f, 0.80f, 1.0f, 0.22f}, 160.0f);

        s.layer("arc", [](LayerBuilder& l) {
            l.position({0.0f, 320.0f, 0.0f});
            l.glow(34.0f, 1.10f, {0.12f, 0.78f, 1.0f, 1.0f});
            l.rounded_rect("curve", {
                .size = {1380.0f, 230.0f},
                .radius = 118.0f,
                .color = {0.14f, 0.64f, 1.0f, 0.14f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.10f, 0.48f, 1.0f, 0.08f}},
                        {0.52f, {0.20f, 0.92f, 1.0f, 0.20f}},
                        {1.0f, {0.10f, 0.36f, 0.92f, 0.12f}},
                    }
                )
            });
        });

        s.layer("ae_badge", [](LayerBuilder& l) {
            l.position({0.0f, -338.0f, 0.0f});
            l.rounded_rect("badge", {
                .size = {98.0f, 98.0f},
                .radius = 22.0f,
                .color = {0.94f, 0.97f, 1.0f, 0.98f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.97f, 0.99f, 1.0f, 1.0f}},
                        {1.0f, {0.60f, 0.86f, 1.0f, 0.98f}},
                    }
                )
            });
            l.text("ae", {
                .text = "Ae",
                .size = {98.0f, 98.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 38.0f,
                .color = {0.10f, 0.13f, 0.22f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
        });

        s.layer("sparkles", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.circle("left", {.radius = 16.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}, .pos = {-720.0f, -40.0f, 0.0f}});
            l.circle("right", {.radius = 16.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}, .pos = {720.0f, -40.0f, 0.0f}});
        });

        s.layer("hero", [=](LayerBuilder& l) {
            l.position({0.0f, -8.0f, 0.0f});
            l.glow(66.0f, 1.55f, {0.18f, 0.76f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 24.0f}, {0.0f, 0.10f, 0.38f, 0.78f}, 34.0f);
            l.text("title", premium::hero_text(
                "SaaS",
                {980.0f, 220.0f},
                {0.0f, -52.0f, 0.0f},
                150.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                        {0.35f, {0.87f, 0.96f, 1.0f, 1.0f}},
                        {1.0f, {0.34f, 0.72f, 1.0f, 1.0f}},
                    }
                ),
                {0.22f, 0.56f, 1.0f, 1.0f},
                2.0f,
                -1.0f,
                VerticalAlign::Middle,
                hero_shadow
            ));
            l.text("subtitle", premium::subtitle_text(
                "FULL TUTORIAL",
                {840.0f, 72.0f},
                {0.0f, 150.0f, 0.0f},
                28.0f,
                {0.94f, 0.97f, 1.0f, 0.92f},
                8.0f,
                subtitle_shadow
            ));
        });

        s.layer("hero_hi", [=](LayerBuilder& l) {
            l.opacity(0.17f);
            l.text("title_hi", premium::hero_text(
                "SaaS",
                {920.0f, 220.0f},
                {0.0f, -72.0f, 0.0f},
                140.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 0.95f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 0.95f}},
                        {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}},
                    }
                ),
                {0.0f, 0.0f, 0.0f, 0.0f},
                0.0f,
                -2.0f,
                VerticalAlign::Middle,
                hero_shadow
            ));
        });

        s.layer("floor_arc", [](LayerBuilder& l) {
            l.position({0.0f, 378.0f, 0.0f});
            l.glow(20.0f, 1.0f, {0.16f, 0.78f, 1.0f, 1.0f});
            l.rect("arc", {
                .size = {1380.0f, 12.0f},
                .color = {0.25f, 0.86f, 1.0f, 0.82f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.06f, 0.45f, 1.0f, 0.0f}},
                        {0.5f, {0.20f, 0.92f, 1.0f, 0.88f}},
                        {1.0f, {0.06f, 0.45f, 1.0f, 0.0f}},
                    }
                )
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

CHRONON_REGISTER_COMPOSITION("TextPremiumHeroSaaSBlue", chronon3d::content::text::text_premium_hero_saas_blue)
