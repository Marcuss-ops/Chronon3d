#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_focus_quote() {
    return composition({.name = "MinimalistFocusQuote", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("background", [](auto& l) {
            l.cache_static();
            l.pin_to(Anchor::Center);
            l.grid_background("grid_bg", {
                .size = {1920.0f, 1080.0f},
                .offset = {0.0f, 0.0f},
                .bg_color = {0.035f, 0.036f, 0.040f, 1.0f},
                .grid_color = {0.68f, 0.70f, 0.74f, 0.05f},
                .spacing = 128.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        s.layer("quote_text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            // progressive blur fade-in animation: start with 16px blur, over 50 frames
            l.focus_in(16.0f, Frame{50});

            l.rounded_rect("text_backdrop", {
                .size = {1580.0f, 380.0f},
                .radius = 28.0f,
                .color = Color{0.0f, 0.0f, 0.0f, 0.22f},
                .pos = {0.0f, 0.0f, 0.0f}
            });

            l.text("quote_text", {
                .text = "Minimalism is not emptiness.\nIt is the discipline of keeping\nonly what matters.",
                .size = {1450.0f, 340.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_size = 58.0f,
                .color = {0.94f, 0.94f, 0.94f, 1.0f},
                .align = TextAlign::Center,
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

// Registration handled by MinimalistModule in minimalist_module.cpp
