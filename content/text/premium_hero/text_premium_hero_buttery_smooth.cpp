#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text {

Composition text_premium_hero_buttery_smooth() {
    return composition({.name = "TextPremiumHeroButterySmooth", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto buttery_shadow = premium::shadow_style(
            {0.10f, 0.02f, 0.12f, 1.0f},
            {0.36f, 0.08f, 0.34f, 1.0f},
            {0.0f, 8.0f},
            16.0f,
            0.24f,
            {0.0f, 52.0f},
            170.0f,
            0.06f
        );
        const auto smooth_shadow = premium::shadow_style(
            {0.08f, 0.04f, 0.14f, 1.0f},
            {0.18f, 0.20f, 0.42f, 1.0f},
            {0.0f, 8.0f},
            14.0f,
            0.22f,
            {0.0f, 48.0f},
            150.0f,
            0.05f
        );

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.03f, 0.01f, 0.05f, 1.0f});
        });

        premium::add_ambient_blob(s, "ambient_left", {-420.0f, -30.0f, 0.0f}, 320.0f, {0.86f, 0.14f, 0.82f, 0.14f}, 210.0f, 0.72f);
        premium::add_ambient_blob(s, "ambient_right", {460.0f, 40.0f, 0.0f}, 260.0f, {0.84f, 0.14f, 0.84f, 0.12f}, 190.0f, 0.68f);
        premium::add_premium_grid(s, {1.0f, 1.0f, 1.0f, 0.035f}, 72.0f);
        premium::add_soft_orb(s, "violet_orb", {-340.0f, 0.0f, 0.0f}, 260.0f, {0.92f, 0.16f, 0.88f, 0.18f}, 140.0f);
        premium::add_soft_orb(s, "magenta_orb", {420.0f, 20.0f, 0.0f}, 220.0f, {0.88f, 0.12f, 0.86f, 0.16f}, 120.0f);

        s.layer("frame", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("panel", {
                .size = {1480.0f, 640.0f},
                .radius = 40.0f,
                .color = {0.06f, 0.02f, 0.10f, 0.40f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {0.10f, 0.02f, 0.16f, 0.32f}},
                        {1.0f, {0.26f, 0.04f, 0.22f, 0.34f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 26.0f}, {0.0f, 0.0f, 0.0f, 0.58f}, 52.0f);
            l.glow(22.0f, 0.55f, {0.90f, 0.18f, 0.92f, 1.0f});
        });

        s.layer("hero", [=](LayerBuilder& l) {
            l.position({0.0f, -24.0f, 0.0f});
            l.text("buttery", premium::hero_text(
                "Buttery",
                {680.0f, 180.0f},
                {-200.0f, -10.0f, 0.0f},
                136.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 0.30f, 0.92f, 1.0f}},
                        {0.55f, {0.98f, 0.12f, 0.86f, 1.0f}},
                        {1.0f, {0.94f, 0.18f, 0.98f, 1.0f}},
                    }
                ),
                {0.12f, 0.04f, 0.18f, 1.0f},
                1.0f,
                -3.0f,
                VerticalAlign::Middle,
                buttery_shadow
            ));
            l.text("smooth", premium::hero_text(
                "Smooth",
                {600.0f, 180.0f},
                {410.0f, -10.0f, 0.0f},
                136.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                        {0.55f, {0.95f, 0.98f, 1.0f, 1.0f}},
                        {1.0f, {0.80f, 0.86f, 0.98f, 1.0f}},
                    }
                ),
                {0.88f, 0.16f, 0.82f, 0.0f},
                0.0f,
                -2.0f,
                VerticalAlign::Middle,
                smooth_shadow
            ));
            l.text("asterisk", {
                .text = "*",
                .size = {120.0f, 120.0f},
                .pos = {650.0f, -14.0f, 0.0f},
                .font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                .font_family = "DejaVu Sans",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 92.0f,
                .color = {1.0f, 0.20f, 0.92f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .paint = {
                    .fill = {1.0f, 0.20f, 0.92f, 1.0f},
                    .stroke_enabled = true,
                    .stroke_color = {1.0f, 0.78f, 0.98f, 0.95f},
                    .stroke_width = 1.6f,
                },
                .shadows = {
                    premium::shadow({0.0f, 0.0f}, 16.0f, 0.55f, {1.0f, 0.20f, 0.92f, 1.0f}),
                    premium::shadow({0.0f, 0.0f}, 30.0f, 0.24f, {1.0f, 0.20f, 0.92f, 1.0f}),
                },
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

CHRONON_REGISTER_COMPOSITION("TextPremiumHeroButterySmooth", chronon3d::content::text::text_premium_hero_buttery_smooth)
