#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_focus_quote() {
    return composition({.name = "MinimalistFocusQuote", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        s.layer("quote_text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.focus_in(16.0f, Frame{50});
            add_text_backdrop(l);
            l.text("quote_text", make_text_params("Minimalism is not emptiness.\nIt is the discipline of keeping\nonly what matters."));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp
