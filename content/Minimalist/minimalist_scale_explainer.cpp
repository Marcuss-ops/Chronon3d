#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_scale_explainer() {
    return composition({.name = "MinimalistScaleExplainer", .width = 1920, .height = 1080, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("background", [](auto& l) {
            l.cache_static();
            l.pin_to(Anchor::Center);
            l.grid_background("grid_bg", {
                .size = {1920.0f, 1080.0f},
                .offset = {0.0f, 0.0f},
                .bg_color = {0.028f, 0.030f, 0.035f, 1.0f},
                .grid_color = {0.55f, 0.58f, 0.63f, 0.045f},
                .spacing = 132.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        s.layer("hero_title_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            // scale drop animation: start scale 1.06x, drop to 1.0x over 40 frames with fade-in
            l.scale_drop(1.06f, Frame{40});

            l.rounded_rect("text_backdrop", {
                .size = {1580.0f, 330.0f},
                .radius = 28.0f,
                .color = Color{0.0f, 0.0f, 0.0f, 0.22f},
                .pos = {0.0f, 0.0f, 0.0f}
            });

            l.text("hero_title", {
                .text = "A minimalist frame does not\nask for attention.\nIt protects what matters.",
                .size = {1450.0f, 280.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_size = 58.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.10f,
                .tracking = 0.0f,
                .auto_fit = false,
                .max_lines = 3,
                .min_font_size = 42.0f,
                .max_font_size = 64.0f,
                .overflow = TextOverflow::Clip,
                .wrap = TextWrap::Word
            });
        });


        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

CHRONON_REGISTER_COMPOSITION("MinimalistScaleExplainer", chronon3d::content::minimalist::minimalist_scale_explainer)
