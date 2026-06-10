#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_hero_explainer() {
    return composition({.name = "MinimalistHeroExplainer", .width = 1920, .height = 1080, .duration = 180}, [](const FrameContext& ctx) {
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

        s.layer("hero_title_layer", [frame = ctx.frame.frame](auto& l) {
            l.pin_to(Anchor::Center);

            l.rounded_rect("text_backdrop", {
                .size = {1580.0f, 380.0f},
                .radius = 28.0f,
                .color = Color{0.0f, 0.0f, 0.0f, 0.22f},
                .pos = {0.0f, 0.0f, 0.0f}
            });

            std::string full_text = "A minimalist frame does not\nask for attention.\nIt protects what matters.";
            // Reveal 1.5 characters per frame starting from frame 0
            size_t visible_chars = std::min(full_text.size(), static_cast<size_t>(static_cast<f32>(frame) * 1.5f));
            std::string revealed_text = full_text.substr(0, visible_chars);
            if (revealed_text.empty()) {
                revealed_text = " ";
            }

            l.text("hero_title", {
                .text = revealed_text,
                .size = {1450.0f, 340.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/GoogleSans-Bold.ttf",
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
                .wrap = TextWrap::None
            });
        });



        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp


