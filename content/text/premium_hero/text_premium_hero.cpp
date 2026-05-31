#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text {

Composition text_hero_fresh() {
    return composition({.name = "HeroFresh", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("hero_text", [](auto& l) {
            l.text("title", premium::hero_text(
                "BUTTERY SMOOTH",
                {1360.0f, 210.0f},
                {0.0f, -86.0f, 0.0f},
                118.0f,
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "DejaVu Sans",
                {0.98f, 0.16f, 0.86f, 1.0f},
                Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {1.0f, 0.70f, 0.22f, 1.0f}},
                        {1.0f, {0.98f, 0.16f, 0.86f, 1.0f}},
                    }
                ),
                {0.98f, 0.16f, 0.86f, 1.0f},
                2.0f,
                -2.0f
            ));
        });

        return s.build();
    });
}

Composition text_empty() {
    return composition({.name = "Empty", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        return s.build();
    });
}

Composition text_only() {
    return composition({.name = "TextOnly", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("title", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.text("main", TextParams{
                .text = "CIAO MONDO",
                .size = {1000.0f, 160.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                .font_family = "DejaVu Sans",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 96.0f,
                .color = {0.98f, 0.16f, 0.86f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.0f,
                .tracking = 0.0f,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
